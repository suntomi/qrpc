#pragma once

#include "base/address.h"
#include "base/alarm.h"
#include "base/allocator.h"
#include "base/crypto.h"
#include "base/loop.h"
#include "base/io_processor.h"
#include "base/macros.h"

#include <functional>

namespace base {
    class SessionFactory {
    public:
        struct Config {
            AlarmProcessor &alarm_processor;
            qrpc_time_t session_timeout;
            static inline Config Default() { 
                return { .alarm_processor = NopAlarmProcessor::Instance(), .session_timeout = qrpc_time_sec(120) };
            }
        };
        class Session {
        public:
            class ReconnectionTimeoutCalculator {
            public:
                static constexpr int kMaxRetryCount = 63;
                ReconnectionTimeoutCalculator(qrpc_time_t unit, qrpc_time_t max = qrpc_time_sec(3600)) :
                    unit_(unit), max_(max) {}
                void Shutdown() { retry_count_++; }
                void Connected() { retry_count_ = 0; }
                qrpc_time_t Timeout() const {
                    double unit = (double)(unit_);
                    auto factor = 1ULL << std::min(kMaxRetryCount, retry_count_);
                    auto base = std::min((double)max_, unit * factor);
                    return (qrpc_time_t)(base * random::gen(0.8, 1.2));
                }
            private:
                int retry_count_{0};
                qrpc_time_t unit_, max_;
            };
            struct CloseReason {
                qrpc_close_reason_code_t code;
                int64_t detail_code;
                std::string msg;
                AlarmProcessor::Id alarm_id{AlarmProcessor::INVALID_ID};
            };
        public:
            Session(SessionFactory &f, Fd fd, const Address &addr) : 
                factory_(f), fd_(fd), addr_(addr), last_active_(qrpc_time_now()), close_reason_() {}
            virtual ~Session() {
                if (close_reason_ != nullptr && close_reason_->alarm_id != AlarmProcessor::INVALID_ID) {
                    factory_.alarm_processor().Cancel(close_reason_->alarm_id);
                }
            }
            inline Fd fd() const { return fd_; }
            inline SessionFactory &factory() { return factory_; }
            inline const SessionFactory &factory() const { return factory_; }
            inline const Address &addr() const { return addr_; }
            inline bool closed() const { return close_reason_ != nullptr; }
            inline CloseReason &close_reason() { return *close_reason_; }            
            // Close should not be called inside OnXXXX callbacks of session.
            // Instead, return negative value from them to close.
            inline bool Close(qrpc_close_reason_code_t code,
                int64_t detail_code = 0, const std::string &msg = "") {
                Close({ .code = code, .detail_code = detail_code, .msg = msg });
            }
            bool timeout(qrpc_time_t now, qrpc_time_t timeout, qrpc_time_t &next_check) const {
                return CheckTimeout(last_active_, qrpc_time_now(), timeout, next_check);
            }
            void Touch(qrpc_time_t at) { last_active_ = at; }
            bool Close(const CloseReason &reason) {
                if (!closed()) {
                    logger::info({
                        {"ev","close"},{"fd",fd()},{"a",addr().str()},
                        {"code",reason.code},{"dcode",reason.detail_code},{"rmsg",reason.msg}
                    });
                    SetCloseReason(reason);
                    auto reconnect_timeout = OnShutdown();
                    factory_.Close(*this);
                    auto &ap = factory_.alarm_processor();
                    if (reconnect_timeout > 0 && 
                        reason.code != QRPC_CLOSE_REASON_MIGRATED &&
                        &ap != &NopAlarmProcessor::Instance()) {
                        this->close_reason_->alarm_id = ap.Set(
                            [this]() { return this->Reconnect(); }, qrpc_time_now() + reconnect_timeout
                        );
                        return false;
                    } else {
                        ASSERT(reconnect_timeout == 0);
                        delete this;
                        return true;
                   }
                }
                ASSERT(false);
                return false;
            }
            // virtual functions to override
            virtual int Send(const char *data, size_t sz) {
                return Syscall::Write(fd_, data, sz);
            }
            virtual int OnConnect() { return QRPC_OK; }
            virtual qrpc_time_t OnShutdown() { return 0; } // return 0 to delete the session
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
            inline void SetCloseReason(const CloseReason &reason) {
                close_reason_.reset(new CloseReason(reason));
                ASSERT(close_reason_ != nullptr);
            }
        protected:
            inline qrpc_time_t Reconnect() {
                factory().Open(addr(), [this](Fd fd, const Address &) {
                    // reuse this session with new fd
                    this->close_reason_.reset();
                    this->fd_ = fd;
                    return this;
                });
            }
        protected:
            SessionFactory &factory_;
            Fd fd_;
            Address addr_;
            qrpc_time_t last_active_;
            std::unique_ptr<CloseReason> close_reason_;
        };
        typedef std::function<Session *(Fd, const Address &)> FactoryMethod;
        typedef std::function<void (int)> DnsErrorHandler;
    public:
        SessionFactory(Loop &l, FactoryMethod &&m) :
            sessions_(), loop_(l), alarm_processor_(NopAlarmProcessor::Instance()),
            factory_method_(m), session_timeout_(0ULL) {}
        SessionFactory(Loop &l, FactoryMethod &&m, Config c) :
            sessions_(), loop_(l), alarm_processor_(c.alarm_processor),
            factory_method_(m), session_timeout_(c.session_timeout) {}
        virtual ~SessionFactory() { Fin(); }
        Loop &loop() { return loop_; }
        AlarmProcessor &alarm_processor() { return alarm_processor_; }
        qrpc_time_t session_timeout() const { return session_timeout_; }
        template <class F> F &to() { return static_cast<F&>(*this); }
        template <class F> const F &to() const { return static_cast<const F&>(*this); }
        virtual Session *Open(const Address &a, FactoryMethod m) = 0;
        bool Connect(const std::string &host, int port, FactoryMethod m, DnsErrorHandler eh, int family_pref = AF_INET);
        bool Connect(const std::string &host, int port, FactoryMethod m, int family_pref = AF_INET);
        static int AssignedPort(Fd fd) {
            Address a;
            int r;
            if ((r = Syscall::GetSockAddrFromFd(fd, a)) < 0) {
                logger::error({{"ev", "fail to get sock addr from fd"},{"fd",fd},{"rc",r}});
                return r;
            } else {
                return a.port();
            }
        }
    protected:
        void Init() {
            if (session_timeout() > 0) {
                alarm_id_ = alarm_processor_.Set(
                    [this]() { return this->CheckTimeout(); }, qrpc_time_now() + session_timeout()
                );
            }
        }
        qrpc_time_t CheckTimeout() {
            qrpc_time_t now = qrpc_time_now();
            qrpc_time_t nearest_check = now + session_timeout();
            for (auto s = sessions_.begin(); s != sessions_.end();) {
                qrpc_time_t next_check;
                auto cur = s++;
                if (cur->second->timeout(now, session_timeout(), next_check)) {
                    // inside Close, the entry will be erased
                    cur->second->Close(QRPC_CLOSE_REASON_TIMEOUT, 0, "session timeout");
                } else {
                    nearest_check = std::min(nearest_check, next_check);
                }
            }
            return nearest_check;
        }
    protected:
        virtual Session *Create(int fd, const Address &a, FactoryMethod &m) {
            auto s = m(fd, a);
            sessions_[a] = s;
            return s;
        }
        virtual void Close(Session &s) {
            sessions_.erase(s.addr());
        }
        virtual void Fin() {
            for (const auto &s : sessions_) {
                s.second->Close(QRPC_CLOSE_REASON_LOCAL, 0, "factory closed");
            }
        }
    protected:
        // deletion timing of Session* is severe, so we want to have full control of it.
        std::map<Address, Session*> sessions_;
        FactoryMethod factory_method_;
        Loop &loop_;
        AlarmProcessor &alarm_processor_;
        AlarmProcessor::Id alarm_id_{AlarmProcessor::INVALID_ID};
        qrpc_time_t session_timeout_;
    };
    class TcpSessionFactory : public SessionFactory {
    public:
        class TcpSession : public Session, public IoProcessor {
        public:
            TcpSession(TcpSessionFactory &f, Fd fd, const Address &addr) : 
                Session(f, fd, addr) {}
            inline void MigrateTo(TcpSession *newsession) {
                factory().loop().ModProcessor(fd_, newsession);
                fd_ = INVALID_FD; // invalidate fd_ so that SessionFactory::Close will not close fd_
            }
            inline bool migrated() const { return fd_ == INVALID_FD; }
            // implements IoProcessor
            void OnEvent(Fd fd, const Event &e) override {
                ASSERT(fd == fd_);
                if (Loop::Writable(e)) {
                    int r;
                    if ((r = factory().loop().Mod(fd_, Loop::EV_READ)) < 0) {
                        Close(QRPC_CLOSE_REASON_SYSCALL, r);
                        return;
                    }
                    if ((r = OnConnect()) < 0) {
                        Close(QRPC_CLOSE_REASON_LOCAL, r);
                        return;
                    }
                }
                if (Loop::Readable(e)) {
                    while (true) {
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
                            Close((migrated() ? QRPC_CLOSE_REASON_MIGRATED :
                                (sz == 0 ? QRPC_CLOSE_REASON_REMOTE : QRPC_CLOSE_REASON_LOCAL)), sz);
                            break;
                        }
                    }
                }
            }
        };
    public:
        TcpSessionFactory(Loop &l, FactoryMethod &&m, Config c = Config::Default()) : SessionFactory(l, std::move(m), c) {}
        TcpSessionFactory(Loop &l, AlarmProcessor &ap, qrpc_time_t timeout = qrpc_time_sec(120)) : 
            SessionFactory(l, [this](Fd fd, const Address &a) -> Session* {
                DIE("client should not call this, provide factory via SessionFactory::Connect");
                return (Session *)nullptr;
            }, { .alarm_processor = ap, .session_timeout = timeout}) {}
    public:
        // implements SessionFactory
        Session *Open(const Address &a, FactoryMethod m) override {
            Fd fd = Syscall::Connect(a.sa(), a.salen());
            if (fd == INVALID_FD) {
                return nullptr;
            }
            auto s = Create(fd, a, m);
            int r; // EV_WRITE only for waiting for establishing connection
            if ((r = loop_.Add(s->fd(), dynamic_cast<TcpSession *>(s), Loop::EV_WRITE)) < 0) {
                s->Close(QRPC_CLOSE_REASON_SYSCALL, r);
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
    };
    class TcpListener : public TcpSessionFactory, public IoProcessor {
    public:
        TcpListener(Loop &l, FactoryMethod &&m) : 
            TcpSessionFactory(l, std::move(m)), fd_(INVALID_FD), port_(0) {}
        TcpListener(TcpListener &&rhs) = default;
        Fd fd() const { return fd_; }
        int port() const { return port_; }
        bool Listen(int port) {
            SessionFactory::Init();
            port_ = port;
            if ((fd_ = Syscall::Listen(port_)) < 0) {
                logger::error({{"ev","Syscall::Listen() fails"},{"port",port},{"rc",fd_},{"errno",Syscall::Errno()}});
                return false;
            }
            if (port_ == 0) {
                port_ = AssignedPort(fd_);
                logger::info({{"ev", "listen port auto assigned"},{"proto","tcp"},{"port",port_}});
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
                logger::info({{"ev","accept"},{"proto","tcp"},{"lfd",fd_},{"fd",afd},{"a",a.str()}});
                auto s = Create(afd, a, factory_method_);                
                int r;
                if ((r = loop_.Add(s->fd(), dynamic_cast<TcpSession *>(s), Loop::EV_READ)) < 0) {
                    s->Close(QRPC_CLOSE_REASON_SYSCALL, r);
                    continue;
                }
                if ((r = s->OnConnect()) < 0) {
                    s->Close(QRPC_CLOSE_REASON_LOCAL, r);
                    continue;
                }
            }
        }
        void Fin() override {
            SessionFactory::Fin();
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
    protected:
        Fd fd_;
        int port_;
    };
    template <class S>
    class TcpListenerOf : public TcpListener {
    public:
        TcpListenerOf(Loop &l, FactoryMethod &&m) : TcpListener(l, std::move(m)) {}
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
    public:
        class UdpSession : public Session {
        public:
            UdpSession(UdpSessionFactory &f, Fd fd, const Address &addr) : 
                Session(f, fd, addr) { AllocIovec(); }
            UdpSessionFactory &udp_session_factory() { return factory().to<UdpSessionFactory>(); }
            const UdpSessionFactory &udp_session_factory() const { return factory().to<UdpSessionFactory>(); }
            std::vector<struct iovec> &write_vecs() { return write_vecs_; }
            // implements Session
            int Send(const char *data, size_t sz) override {
                return Write(data, sz, addr());
            }
        protected:
            bool AllocIovec() {
                auto b = udp_session_factory().write_buffers_.Alloc();
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
                        udp_session_factory().write_buffers_.Free(iov.iov_base);
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
                        logger::error({{"ev","SendTo fails"},{"fd",fd_},{"errno",Syscall::Errno()},{"merr",Syscall::StrError()}});
                        ASSERT(false);
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
                return QRPC_OK;
            }
        private:
            std::vector<struct iovec> write_vecs_;
        };
    public:
        UdpSessionFactory(Loop &l, FactoryMethod &&m, Config config = Config::Default()) :
            SessionFactory(l, std::move(m), config), fd_(INVALID_FD), port_(0),
            #if defined(__QRPC_USE_RECVMMSG__)
                batch_size_(256),
            #else
                batch_size_(1),
            #endif
            factory_method_(m),
            read_packets_(batch_size_), read_buffers_(batch_size_), write_buffers_(batch_size_) {
            SetupPacket();
        }
        UdpSessionFactory(Loop &l, AlarmProcessor &ap, qrpc_time_t session_timeout = qrpc_time_sec(120)) :
            UdpSessionFactory(l, [this](Fd fd, const Address &ap) {
                DIE("client should not call this, provide factory with SessionFactory::Connect");
                return (Session *)nullptr;
            }, {.alarm_processor = ap, .session_timeout = session_timeout}) {}
        UdpSessionFactory(UdpSessionFactory &&rhs) = default;
        Fd fd() const { return fd_; }
        int port() const { return port_; }
        bool Bind() { return Init(0); } //automatically allocate available port
        bool Init(int port) {
            SessionFactory::Init();
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
            if (Syscall::Bind(fd_, port_) != QRPC_OK) {
                Syscall::Close(fd_);
                fd_ = INVALID_FD;
                return false;
            }
            if (port_ == 0) {
                port_ = AssignedPort(fd_);
                logger::info({{"ev", "listen port auto assigned"},{"proto","tcp"},{"port",port_}});
            }
            if (loop_.Add(fd_, this, Loop::EV_READ) < 0) {
                logger::error({{"ev","Loop::Add fails"},{"fd",fd_}});
                Syscall::Close(fd_);
                fd_ = INVALID_FD;
                return false;
            }
            return true;
        }
        Session *Open(const Address &a, FactoryMethod m) override {
            ASSERT(fd_ >= 0); // forget to call Bind or Listen?
            auto s = Create(fd_, a, m);
            if (s == nullptr) {
                ASSERT(false);
                return nullptr;
            }
            int r;
            if ((r = s->OnConnect()) < 0) {
                s->Close(QRPC_CLOSE_REASON_LOCAL, r, "OnConnect() fails");
                return nullptr;
            }
            return s;
        }
        void Fin() override {
            SessionFactory::Fin();
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
        int batch_size_;
        bool overflow_supported_;
        FactoryMethod factory_method_;
        std::vector<mmsghdr> read_packets_;
        std::vector<ReadPacketBuffer> read_buffers_;
        Allocator<WritePacketBuffer> write_buffers_;
    };
    class UdpListener : public UdpSessionFactory {
    public:
        UdpListener(Loop &l, FactoryMethod &&m, const Config c = Config::Default()) :
            UdpSessionFactory(l, std::move(m), c) {}
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
        UdpListenerOf(Loop &l, FactoryMethod &&m, const Config c = Config::Default()) : UdpListener(l, std::move(m), c) {}
        UdpListenerOf(Loop &l, const Config c = Config::Default()) : UdpListener(l, [this](Fd fd, const Address &a) {
            static_assert(std::is_base_of<Session, S>(), "S must be a descendant of Session");
            return new S(*this, fd, a);
        }, c) {}
    };
    // external typedef
    typedef SessionFactory::Session Session;
    typedef TcpSessionFactory::TcpSession TcpSession;
    typedef UdpSessionFactory::UdpSession UdpSession;
}