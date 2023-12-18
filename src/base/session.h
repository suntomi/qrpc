#pragma once

#include "base/address.h"
#include "base/alarm.h"
#include "base/allocator.h"
#include "base/loop.h"
#include "base/io_processor.h"
#include "base/macros.h"

#include <functional>

namespace base {
    class SessionFactory {
    public:
        class Session {
        public:
            struct CloseReason {
                qrpc_close_reason_code_t code;
                int64_t detail_code;
                std::string msg;
            };
        public:
            Session(SessionFactory &f, Fd fd, const Address &addr) : 
                factory_(f), fd_(fd), addr_(addr), close_reason_() {}
            virtual ~Session() {}
            inline Fd fd() const { return fd_; }
            inline SessionFactory &factory() { return factory_; }
            inline const SessionFactory &factory() const { return factory_; }
            inline const Address &addr() const { return addr_; }
            inline bool closed() const { return close_reason_ != nullptr; }
            inline void Close(
                qrpc_close_reason_code_t code, int64_t detail_code = 0, const std::string &msg = ""
            ) {
                Close( { .code = code, .detail_code = detail_code, .msg = msg });
            }
            // virtual functions to override
            virtual void Close(const CloseReason &reason) {
                if (!closed()) {
                    logger::info({
                        {"ev", "close"},{"fd",fd()},{"addr", addr().str()},
                        {"code", reason.code},{"dcode", reason.detail_code},{"rmsg", reason.msg}
                    });
                    SetCloseReason(reason);
                    factory_.Close(*this);
                }
            }
            virtual int Send(const char *data, size_t sz) {
                return Syscall::Write(fd_, data, sz);
            }
            virtual int OnConnect() { return QRPC_OK; }
            virtual void OnShutdown() {}
            virtual int OnRead(const char *p, size_t sz) = 0;
        public:
            static inline bool CheckTimeout(
                qrpc_time_t last_active, qrpc_time_t now, qrpc_time_t timeout, qrpc_time_t &next_check
            ) {
                ASSERT(timeout > 0);
                auto diff = now - last_active;
                if (diff > timeout) {
                    return true;
                } else {
                    next_check = now + timeout - diff;
                    return false;
                }
            }        
        protected:
            inline void SetCloseReason(const CloseReason &reason) {
                close_reason_.reset(new CloseReason(reason));
                ASSERT(close_reason_ != nullptr);
            }
        protected:
            SessionFactory &factory_;
            Fd fd_;
            Address addr_;
            std::unique_ptr<CloseReason> close_reason_;
        };
        typedef std::function<Session *(Fd, const Address &)> FactoryMethod;
    public:
        SessionFactory(Loop &l, FactoryMethod &m) : 
            sessions_(), loop_(l), factory_method_(m) {}
        virtual ~SessionFactory() {}
        Loop &loop() { return loop_; }
        template <class F> F &to() { return static_cast<F&>(*this); }
        template <class F> const F &to() const { return static_cast<const F&>(*this); }
        virtual Session *Open(const Address &a, FactoryMethod m) {
            Fd fd = Syscall::Connect(a.sa(), a.salen());
            return Create(fd, a, m);
        }
        bool Resolve(int family_pref, const std::string &host, int port, FactoryMethod m);
    protected:
        virtual Session *Create(int fd, const Address &a, FactoryMethod &m) {
            auto s = m(fd, a);
            sessions_[a] = s;
            return s;
        }
        virtual void Close(Session &s) {
            sessions_.erase(s.addr());
        }
    protected:
        // deletion timing of Session* is severe, so we want to have full control of it.
        std::map<Address, Session*> sessions_;
        FactoryMethod factory_method_;
        Loop &loop_;
    };
    typedef SessionFactory TcpSessionFactory;
    class TcpListener : public TcpSessionFactory, public IoProcessor {
    public:
        class TcpSession : public Session, public IoProcessor {
        public:
            TcpSession(TcpListener &f, Fd fd, const Address &addr) : 
                Session(f, fd, addr) {}
            ~TcpSession() override {}
            inline void MigrateTo(TcpSession *newsession) {
                factory().loop().ModProcessor(fd_, newsession);
                // close this session without closing fd
                SetCloseReason({ .code = QRPC_CLOSE_REASON_MIGRATED, .detail_code = 0, .msg = "" });
            }
            // implements IoProcessor
            void OnEvent(Fd fd, const Event &e) override {
                ASSERT(fd == fd_);
                if (Loop::Readable(e)) {
                    while (LIKELY(!closed())) {
                        char buffer[4096];
                        int sz = sizeof(buffer);
                        if ((sz = Syscall::Read(fd, buffer, sz)) < 0) {
                            int err = Syscall::Errno();
                            if (Syscall::WriteMayBlocked(err, false)) {
                                return;
                            }
                            Close(QRPC_CLOSE_REASON_SYSCALL, err, Syscall::StrError(err));
                            break;
                        }
                        if (sz == 0 || (sz = OnRead(buffer, sz)) < 0) {
                            Close(sz == 0 ? QRPC_CLOSE_REASON_REMOTE : QRPC_CLOSE_REASON_LOCAL, sz);
                            break;
                        }
                    }
                    delete this;
                }
            }
            void OnClose(Fd fd) override {
                ASSERT(fd == fd_);
                OnShutdown();
            }
            int OnOpen(Fd fd) override {
                ASSERT(fd == fd_);
                int r;
                if ((r = OnConnect()) < 0) {
                    return r;
                }
                return QRPC_OK;
            }
        };        
    public:
        TcpListener(Loop &l, FactoryMethod m) : 
            TcpSessionFactory(l, m), fd_(INVALID_FD), port_(0) {}
        TcpListener(TcpListener &&rhs) : 
            TcpSessionFactory(rhs.loop(), rhs.factory_method_), fd_(rhs.fd_), port_(rhs.port_) {
            rhs.fd_ = INVALID_FD;
            rhs.port_ = 0;
        }
        ~TcpListener() override { Close(); }
        Fd fd() const { return fd_; }
        int port() const { return port_; }
        bool Listen(int port) {
            port_ = port;
            if ((fd_ = Syscall::Listen(port)) < 0) {
                logger::error({{"ev","Syscall::Listen() fails"},{"port",port},{"errno",Syscall::Errno()}});
                return fd_;
            }
            if (loop_.Add(fd_, this, Loop::EV_READ) < 0) {
                logger::error({{"ev","Loop::Add fails"},{"fd",fd_}});
                Syscall::Close(fd_);
                fd_ = INVALID_FD;
                return false;
            }
            return true;
        }
        void Accept() {
            while (true) {
                struct sockaddr_storage sa;
                socklen_t salen = sizeof(sa);
                Fd afd = Syscall::Accept(fd_, sa, salen);
                if (afd < 0) {
                    if (Syscall::WriteMayBlocked(Syscall::Errno(), false)) {
                        break;
                    }
                    return;
                }
                auto a = Address(sa, salen);
                logger::info({{"ev", "accept"},{"proto","tcp"},{"afd",afd},{"fd",fd_},{"addr", a.str()}});
                Create(afd, a, factory_method_);
            }
        }
        void Close() {
            if (fd_ != INVALID_FD) {
                loop_.Del(fd_);
                Syscall::Close(fd_);
            }
        }
        // implements IoProcessor
		void OnEvent(Fd fd, const Event &e) {
            if (Loop::Readable(e)) {
                Accept();
            }
        }
		void OnClose(Fd fd) {
            for (const auto &s : sessions_) {
                s.second->Close(QRPC_CLOSE_REASON_LOCAL, 0, "listener closed");
            }
        }
		int OnOpen(Fd fd) {
            return QRPC_OK;
        }
        // implements SessionFactory
       Session *Create(Fd fd, const Address &a, FactoryMethod &m) override {
            auto s = m(fd, a);
            sessions_[a] = s;
            if (loop_.Add(s->fd(), dynamic_cast<TcpSession *>(s), Loop::EV_READ | Loop::EV_WRITE) < 0) {
                Close(*s);
                return nullptr;
            }
            return s;
        }
        void Close(Session &s) override {
            Fd fd = s.fd();
            if (fd != INVALID_FD) {
                sessions_.erase(s.addr());
                loop_.Del(fd);
                Syscall::Close(fd);
            }
        }
    protected:
        Fd fd_;
        int port_;
    };
    template <class S>
    class TcpListenerOf : public TcpListener {
    public:
        TcpListenerOf(Loop &l, FactoryMethod m) : TcpListener(l, m) {}
        TcpListenerOf(Loop &l) : TcpListener(l, [this](Fd fd, const Address &a) {
            static_assert(std::is_base_of<TcpSession, S>(), "S must be a descendant of TcpSession");
            return new S(*this, fd, a);
        }) {}
    };
    class UdpSessionFactory : public SessionFactory, public IoProcessor {
    public:
        #if !defined(__QRPC_USE_RECVMMSG__)
        struct mmsghdr {
            struct msghdr msg_hdr;
            unsigned int msg_len;
        };
        #endif
        struct ReadPacketBuffer {
            struct cmsghdr chdr;
            struct iovec iov;
            char buf[Syscall::kMaxIncomingPacketSize];
            char cbuf[Syscall::kDefaultUdpPacketControlBufferSize];
            sockaddr_storage sa;
        };
        struct WritePacketBuffer {
            char buf[Syscall::kMaxOutgoingPacketSize];
            char padd[4];
        };
        struct Config {
            AlarmProcessor &alarm_processor;
            qrpc_time_t session_timeout;
            static inline Config Default() { 
                return { .alarm_processor = NopAlarmProcessor::Instance(), .session_timeout = qrpc_time_sec(120) };
            }
        };
    public:
        class UdpSession : public Session {
        public:
            UdpSession(UdpSessionFactory &f, Fd fd, const Address &addr) : 
                Session(f, fd, addr), last_active_(qrpc_time_now()) { AllocIovec(); }
            ~UdpSession() override {}
            UdpSessionFactory &listener() { return factory().to<UdpSessionFactory>(); }
            const UdpSessionFactory &listener() const { return factory().to<UdpSessionFactory>(); }
            bool timeout(qrpc_time_t now, qrpc_time_t timeout, qrpc_time_t &next_check) const {
                return CheckTimeout(last_active_, qrpc_time_now(), timeout, next_check);
            }
            std::vector<struct iovec> &write_vecs() { return write_vecs_; }
            // implements Session
            int Send(const char *data, size_t sz) override {
                return Write(data, sz, addr());
            }
            void Touch(qrpc_time_t at) { last_active_ = at; }
        protected:
            bool AllocIovec() {
                auto b = listener().write_buffers_.Alloc();
                if (b == nullptr) {
                    logger::error({{"ev","write_buffers_.Alloc fails"}});
                    ASSERT(false);
                    return false;
                }
                write_vecs_.push_back({ .iov_base = b, .iov_len = 0 });
            }
            void Reset() {
                auto size = write_vecs_.size();
                // free additional buffers
                if (size > 1) {
                    for (int i = 0; i < size - 1; i++) {
                        struct iovec &iov = write_vecs_.back();
                        listener().write_buffers_.Free(iov.iov_base);
                        write_vecs_.pop_back();
                    }
                }
                auto &iov = write_vecs_[0];
                iov.iov_len = 0;                
            }
            int Flush() {
                for (auto &iov : write_vecs_) {
                    if (iov.iov_len <= 0) {
                        continue;
                    }
                    struct msghdr h;
                    h.msg_name = const_cast<sockaddr *>(addr().sa());
                    h.msg_namelen = addr().salen();
                    h.msg_iov = &iov;
                    h.msg_iovlen = 1;
                    h.msg_control = nullptr;
                    h.msg_controllen = 0;
                    if (Syscall::SendTo(fd_, &h) < 0) {
                        return QRPC_ESYSCALL;
                    }                    
                }
                Reset();
                return QRPC_OK;
            }
            int Write(const char *p, size_t sz, const Address &a) {
                int r;
                ASSERT(write_vecs_.size() > 0);
                while (sz > 0) {
                    struct iovec &curr_iov = write_vecs_[write_vecs_.size() - 1];
                    if (curr_iov.iov_len + sz > Syscall::kMaxOutgoingPacketSize) {
                    #if defined(__QRPC_USE_RECVMMSG__)
                        if (AllocIovec()) {
                            return QRPC_EALLOC;
                        }
                        curr_iov = write_vecs_[write_vecs_.size() - 1];
                    #else
                        if ((r = Flush()) < 0) { return r; }
                    #endif
                    }
                    size_t copied = std::min(sz, Syscall::kMaxOutgoingPacketSize - curr_iov.iov_len);
                    Syscall::MemCopy(
                        reinterpret_cast<char *>(curr_iov.iov_base) + curr_iov.iov_len, p, copied
                    );
                    curr_iov.iov_len += copied;
                    p += copied;
                    sz -= copied;
                }
                if ((r = Flush()) < 0) { return r; }
            }
        private:
            std::vector<struct iovec> write_vecs_;
            qrpc_time_t last_active_;
        };
    public:
        UdpSessionFactory(Loop &l, FactoryMethod m, const Config config = Config::Default()) :
            SessionFactory(l, m), fd_(INVALID_FD), port_(0),
            #if defined(__QRPC_USE_RECVMMSG__)
                batch_size_(256),
            #else
                batch_size_(1),
            #endif
            alarm_processor_(config.alarm_processor), 
            timeout_(config.session_timeout), 
            factory_method_(m),
            read_packets_(batch_size_), read_buffers_(batch_size_),
            write_buffers_(batch_size_) {
            SetupPacket();
        }
        UdpSessionFactory(UdpSessionFactory &&rhs) : 
            SessionFactory(rhs.loop(), rhs.factory_method_), fd_(rhs.fd_), port_(rhs.port_),
            batch_size_(rhs.batch_size_), alarm_processor_(rhs.alarm_processor_), 
            timeout_(rhs.timeout_), factory_method_(rhs.factory_method_),
            read_packets_(std::move(rhs.read_packets_)), read_buffers_(std::move(rhs.read_buffers_)),
            write_buffers_(std::move(rhs.write_buffers_)) {
            rhs.fd_ = INVALID_FD;
            rhs.port_ = 0;
            rhs.batch_size_ = 0;
            rhs.alarm_processor_ = NopAlarmProcessor::Instance();
            rhs.timeout_ = 0;
            rhs.factory_method_ = nullptr;
        }
        ~UdpSessionFactory() override { Close(); }
        Fd fd() const { return fd_; }
        int port() const { return port_; }
        qrpc_time_t timeout() const { return timeout_; }
        AlarmProcessor &alarm_processor() { return alarm_processor_; }
        bool Bind() { return Init(0); } //automatically allocate available port
        bool Init(int port) {
            if (fd_ != INVALID_FD) {
                ASSERT(port_ != 0);
                logger::warn({{"ev","already initialized"},{"fd",fd_},{"port",port_}});
                return true;
            }
            port_ = port;
            // create udp socket
            if ((fd_ = Syscall::CreateUDPSocket(AF_INET, &overflow_supported_)) < 0) {
                return false;
            }
            if (Syscall::Bind(fd_, port) != QRPC_OK) {
                Syscall::Close(fd_);
                fd_ = INVALID_FD;
                return false;
            }
            if (loop_.Add(fd_, this, Loop::EV_READ) < 0) {
                logger::error({{"ev","Loop::Add fails"},{"fd",fd_}});
                Syscall::Close(fd_);
                fd_ = INVALID_FD;
                return false;
            }
            if (timeout() > 0) {
                alarm_processor_.Set(
                    [this]() { return this->CheckTimeout(); }, qrpc_time_now() + timeout()
                );
            }
            return true;
        }
        Session *Open(const Address &a, FactoryMethod m) override {
            return Create(fd_, a, m);
        }
        void Close() {
            if (fd_ != INVALID_FD) {
                loop_.Del(fd_);
                Syscall::Close(fd_);
                fd_ = INVALID_FD;
            }
            if (alarm_id_ != AlarmProcessor::INVALID_ID) {
                alarm_processor_.Cancel(alarm_id_);
                alarm_id_ = AlarmProcessor::INVALID_ID;
            }
        }
        int Read();
        qrpc_time_t CheckTimeout() {
            qrpc_time_t now = qrpc_time_now();
            qrpc_time_t nearest_check = now + timeout_;
            for (auto s = sessions_.begin(); s != sessions_.end();) {
                qrpc_time_t next_check;
                auto cur = s++;
                if (dynamic_cast<UdpSession *>(cur->second)->timeout(now, timeout_, next_check)) {
                    // inside Close, the entry will be erased
                    cur->second->Close(QRPC_CLOSE_REASON_LOCAL, 0, "session timeout");
                    delete cur->second;
                } else {
                    nearest_check = std::min(nearest_check, next_check);
                }
            }
            return nearest_check;
        }
        void SetupPacket();
        void ProcessPackets(int count);
        // implements IoProcessor
		void OnEvent(Fd fd, const Event &e) {
            if (Loop::Readable(e)) {
                int r;
                while (true) {
                    if ((r = Read()) < 0) {
                        break;
                    } else {
                        ProcessPackets(r);
                    }
                }
            }
        }
		void OnClose(Fd fd) {
            for (const auto &s : sessions_) {
                s.second->Close(QRPC_CLOSE_REASON_LOCAL, 0, "listener closed");
            }
        }
		int OnOpen(Fd fd) {
            return QRPC_OK;
        }
    protected:
        Fd fd_;
        int port_;
        AlarmProcessor &alarm_processor_;
        int batch_size_;
        qrpc_time_t timeout_;
        AlarmProcessor::Id alarm_id_{AlarmProcessor::INVALID_ID};
        bool overflow_supported_;
        FactoryMethod factory_method_;
        std::vector<mmsghdr> read_packets_;
        std::vector<ReadPacketBuffer> read_buffers_;
        Allocator<WritePacketBuffer> write_buffers_;
    };
    class UdpListener : public UdpSessionFactory {
    public:
        UdpListener(Loop &l, FactoryMethod m, const Config c = Config::Default()) : UdpSessionFactory(l, m, c) {}
        bool Listen(int port) { return Init(port); }
    };
    class AdhocUdpListener : public UdpListener {
    public:
        class AdhocUdpSession : public UdpSession {
        public:
            AdhocUdpSession(AdhocUdpListener &f, Fd fd, const Address &addr) : 
                UdpSession(f, fd, addr) {}
            ~AdhocUdpSession() override {}
            int OnRead(const char *p, size_t sz) override {
                return factory().to<AdhocUdpListener>().handler()(*this, p, sz);
            }
        };
    public:
        typedef std::function<int (AdhocUdpSession &, const char *, size_t)> Handler;
        AdhocUdpListener(Loop &l, Handler h, const Config config = Config::Default()) :
            UdpListener(l, [this](Fd fd, const Address &a) {
                return new AdhocUdpSession(*this, fd, a);
            }, config), handler_(h) {}
        inline Handler &handler() { return handler_; }
    private:
       Handler handler_;
    };
    typedef AdhocUdpListener::AdhocUdpSession AdhocUdpSession;
    template <class S>
    class UdpListenerOf : public UdpListener {
    public:
        UdpListenerOf(Loop &l, FactoryMethod m, const Config c = Config::Default()) : UdpListener(l, m, c) {}
        UdpListenerOf(Loop &l, const Config c = Config::Default()) : UdpListener(l, [this](Fd fd, const Address &a) {
            static_assert(std::is_base_of<Session, S>(), "S must be a descendant of Session");
            return new S(*this, fd, a);
        }, c) {}
    };
    // external typedef
    typedef SessionFactory::Session Session;
    typedef TcpListener::TcpSession TcpSession;
    typedef UdpListener::UdpSession UdpSession;
}