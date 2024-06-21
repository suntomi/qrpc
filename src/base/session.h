#pragma once

#include "base/address.h"
#include "base/alarm.h"
#include "base/allocator.h"
#include "base/crypto.h"
#include "base/loop.h"
#include "base/io_processor.h"
#include "base/macros.h"
#include "base/resolver.h"

#include <functional>

namespace base {
    class SessionFactory {
    public:
        struct Config {
            Config(Resolver &r, qrpc_time_t st) : resolver(r), session_timeout(st) {}
            static inline Config Default() { 
                // default no timeout
                return Config(NopResolver::Instance(), qrpc_time_sec(0));
            }
        public:
            Resolver &resolver;
            qrpc_time_t session_timeout;
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
                    // TODO: configurable jitter
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
            DISALLOW_COPY_AND_ASSIGN(Session);
            inline Fd fd() const { return fd_; }
            inline SessionFactory &factory() { return factory_; }
            inline const SessionFactory &factory() const { return factory_; }
            inline const Address &addr() const { return addr_; }
            inline bool closed() const { return close_reason_ != nullptr; }
            inline CloseReason &close_reason() { return *close_reason_; }
            inline bool timeout(qrpc_time_t now, qrpc_time_t timeout, qrpc_time_t &next_check) const {
                return CheckTimeout(last_active_, qrpc_time_now(), timeout, next_check);
            }
            // Close should not be called inside OnXXXX callbacks of session.
            // Instead, return negative value from them to close, or call Shutdown()
            inline bool Close(qrpc_close_reason_code_t code,
                int64_t detail_code = 0, const std::string &msg = "") {
                return Close({ .code = code, .detail_code = detail_code, .msg = msg });
            }
            // this closes session and safely callable from OnRead(), OnConnect()
            // but recommended to return negative value from them to close. use this only when there is no other way.
            inline bool ScheduleClose(qrpc_close_reason_code_t code,
                int64_t detail_code = 0, const std::string &msg = "") {
                // set close reason first so that cancel the alarm when session is deleted before alarm raised
                SetCloseReason({ .code = code, .detail_code = detail_code, .msg = msg });
                this->close_reason_->alarm_id = factory().alarm_processor().Set(
                    [this]() {
                        std::unique_ptr<CloseReason> cr = std::move(this->close_reason_);
                        ASSERT(close_reason_ == nullptr);
                        if (cr != nullptr) {
                            this->Close(*cr);
                        } else {
                            QRPC_LOGJ(error,{{"ev","alarm fired but close reason is already reset"}});
                            ASSERT(false);
                            this->Close(QRPC_CLOSE_REASON_SHUTDOWN, 0, "invalid shutdown: no close reason");
                        }
                        return 0; // stop alarm
                    }, qrpc_time_now()
                );
            }
            inline void Touch(qrpc_time_t at) { last_active_ = at; }
            // virtual functions to override
            virtual int Send(const char *data, size_t sz) {
                return Syscall::Write(fd_, data, sz);
            }
            virtual int OnConnect() { return QRPC_OK; }
            virtual qrpc_time_t OnShutdown() { return 0; } // return 0 to delete the session
            virtual int OnRead(const char *p, size_t sz) = 0;
            virtual const char *proto() const = 0;
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
                        // if session is migrated or shutdown, reconnect setting is ignored
                        reason.code != QRPC_CLOSE_REASON_MIGRATED &&
                        reason.code != QRPC_CLOSE_REASON_SHUTDOWN &&
                        &ap != &NopAlarmProcessor::Instance()) {
                        this->close_reason_->alarm_id = ap.Set(
                            [this]() { return this->Reconnect(); }, qrpc_time_now() + reconnect_timeout
                        );
                        return false;
                    } else {
                        ASSERT(reconnect_timeout == 0 ||
                            reason.code == QRPC_CLOSE_REASON_MIGRATED ||
                            reason.code == QRPC_CLOSE_REASON_SHUTDOWN);
                        ASSERT(this->close_reason_->alarm_id == AlarmProcessor::INVALID_ID);
                        delete this;
                        return true;
                   }
                }
                ASSERT(false);
                return false;
            }
            inline qrpc_time_t Reconnect() {
                factory().Open(addr(), [this](Fd fd, const Address &) {
                    // reuse this session with new fd
                    this->close_reason_.reset();
                    this->fd_ = fd;
                    return this;
                });
                return 0; // stop alarm
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
            loop_(l), resolver_(NopResolver::Instance()), alarm_processor_(l.alarm_processor()),
            factory_method_(m), session_timeout_(0ULL) { Init(); }
        SessionFactory(Loop &l, FactoryMethod &&m, Config c) :
            loop_(l), resolver_(c.resolver), alarm_processor_(l.alarm_processor()),
            factory_method_(m), session_timeout_(c.session_timeout) { Init(); }
        SessionFactory(SessionFactory &&rhs);
        DISALLOW_COPY_AND_ASSIGN(SessionFactory);
        void Move(SessionFactory &&rhs);
        virtual ~SessionFactory() {}
        virtual Session *Open(const Address &a, FactoryMethod m) = 0;
        inline Loop &loop() { return loop_; }
        inline AlarmProcessor &alarm_processor() { return alarm_processor_; }
        inline qrpc_time_t session_timeout() const { return session_timeout_; }
        template <class F> F &to() { return static_cast<F&>(*this); }
        template <class F> const F &to() const { return static_cast<const F&>(*this); }
        bool Connect(const std::string &host, int port, FactoryMethod m, DnsErrorHandler eh, int family_pref = AF_INET);
        bool Connect(const std::string &host, int port, FactoryMethod m, int family_pref = AF_INET);
        static inline int AssignedPort(Fd fd) {
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
        inline void Init() {
            if (session_timeout() > 0) {
                alarm_id_ = alarm_processor_.Set(
                    [this]() { return this->CheckTimeout(); }, qrpc_time_now() + session_timeout()
                );
            }
        }
        template <class SESSIONS>
        qrpc_time_t CheckSessionTimeout(SESSIONS &sessions) {
            qrpc_time_t now = qrpc_time_now();
            qrpc_time_t nearest_check = now + session_timeout();
            for (auto it = sessions.begin(); it != sessions.end();) {
                auto cur = it++;
                qrpc_time_t next_check;
                if (cur->second->timeout(now, session_timeout(), next_check)) {
                    // inside Close, the entry will be erased
                    cur->second->Close(QRPC_CLOSE_REASON_TIMEOUT, 0, "session timeout");
                } else {
                    nearest_check = std::min(nearest_check, next_check);
                }
            };
            return nearest_check;
        }
        template <class SESSIONS>
        void FinSessions(SESSIONS &sessions) {
            for (auto it = sessions.begin(); it != sessions.end();) {
                auto cur = it++;
                cur->second->Close(QRPC_CLOSE_REASON_SHUTDOWN, 0, "factory closed");
            }
            if (alarm_id_ != AlarmProcessor::INVALID_ID) {
                alarm_processor_.Cancel(alarm_id_);
                alarm_id_ = AlarmProcessor::INVALID_ID;
            }
        }
    protected:
        virtual Session *Create(int fd, const Address &a, FactoryMethod &m) = 0;
        virtual void Close(Session &s) = 0;
        virtual qrpc_time_t CheckTimeout() = 0;
    protected:
        FactoryMethod factory_method_;
        Loop &loop_;
        Resolver &resolver_;
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
            DISALLOW_COPY_AND_ASSIGN(TcpSession);
            inline void MigrateTo(TcpSession *newsession) {
                factory().loop().ModProcessor(fd_, newsession);
                fd_ = INVALID_FD; // invalidate fd_ so that SessionFactory::Close will not close fd_
            }
            inline bool migrated() const { return fd_ == INVALID_FD; }
            // implements Session
            const char *proto() const { return "tcp"; }
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
        TcpSessionFactory(Loop &l, FactoryMethod &&m, Config c = Config::Default()) :
            SessionFactory(l, std::move(m), c), sessions_() {}
        TcpSessionFactory(TcpSessionFactory &&rhs) :
            SessionFactory(std::move(rhs)), sessions_(std::move(rhs.sessions_)) {}
        ~TcpSessionFactory() override { Fin(); }
        DISALLOW_COPY_AND_ASSIGN(TcpSessionFactory);
        void Fin() { FinSessions(sessions_); }
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
    protected:
        // implements SessionFactory
        void Close(Session &s) override {
            Fd fd = s.fd();
            if (fd != INVALID_FD) {
                loop_.Del(fd);
                Syscall::Close(fd);
                sessions_.erase(fd);
            }
        }
        Session *Create(int fd, const Address &a, FactoryMethod &m) override {
            auto s = m(fd, a);
            sessions_[fd] = s;
            return s;
        }
        qrpc_time_t CheckTimeout() override { return CheckSessionTimeout(sessions_); }
    protected:
        // deletion timing of Session* is severe, so we want to have full control of it.
        std::map<Fd, Session*> sessions_;
    };
    class TcpClient : public TcpSessionFactory {
    public:
        TcpClient(Loop &l, Resolver &r, qrpc_time_t timeout = qrpc_time_sec(120)) : 
            TcpSessionFactory(l, [this](Fd fd, const Address &a) -> Session* {
                DIE("client should not call this, provide factory via SessionFactory::Connect");
                return (Session *)nullptr;
            }, Config(r, timeout)) {}
        TcpClient(TcpClient &&rhs) : TcpSessionFactory(std::move(rhs)) {}
        DISALLOW_COPY_AND_ASSIGN(TcpClient);
    };
    class TcpListener : public TcpSessionFactory, public IoProcessor {
    public:
        TcpListener(Loop &l, FactoryMethod &&m, Config c = Config::Default()) : 
            TcpSessionFactory(l, std::move(m), c), fd_(INVALID_FD), port_(0) {}
        TcpListener(TcpListener &&rhs) : TcpSessionFactory(std::move(rhs)), 
            fd_(rhs.fd_), port_(rhs.port_) { rhs.fd_ = INVALID_FD; }
        ~TcpListener() override { Fin(); }
        DISALLOW_COPY_AND_ASSIGN(TcpListener);
        Fd fd() const { return fd_; }
        int port() const { return port_; }
        void Fin() {
            TcpSessionFactory::Fin();
            if (fd_ != INVALID_FD) {
                loop_.Del(fd_);
                Syscall::Close(fd_);
                fd_ = INVALID_FD;
            }
        }
        bool Listen(int port) {
            ASSERT(fd_ == INVALID_FD);
            port_ = port;
            if ((fd_ = Syscall::Listen(port_)) < 0) {
                logger::error({{"ev","Syscall::Listen() fails"},{"port",port},{"rc",fd_},{"errno",Syscall::Errno()}});
                return false;
            }
            if (port_ == 0) {
                if ((port_ = AssignedPort(fd_)) < 0) {
                    QRPC_LOGJ(error, {{"ev","AssignedPort() fails"},{"fd",fd_},{"r",port_}});
                    Syscall::Close(fd_);
                    fd_ = INVALID_FD;
                    return false;
                }
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
        // implements IoProcessor
		void OnEvent(Fd fd, const Event &e) override {
            if (Loop::Readable(e)) {
                Accept();
            }
        }
    protected:
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
    protected:
        Fd fd_;
        int port_;
    };
    template <class S>
    class TcpListenerOf : public TcpListener {
    public:
        TcpListenerOf(Loop &l, FactoryMethod &&m, Config c = Config::Default()) :
            TcpListener(l, std::move(m), c) {}
        TcpListenerOf(Loop &l) : TcpListener(l, [this](Fd fd, const Address &a) {
            static_assert(std::is_base_of<TcpSession, S>(), "S must be a descendant of TcpSession");
            return new S(*this, fd, a);
        }) {}
        TcpListenerOf(TcpListenerOf &&rhs) : TcpListener(std::move(rhs)) {}
        DISALLOW_COPY_AND_ASSIGN(TcpListenerOf);
    };
    class UdpSessionFactory : public SessionFactory {
    public:
        struct Config : public SessionFactory::Config {
        #if defined(__QRPC_USE_RECVMMSG__)
            static constexpr int BATCH_SIZE = 256;
        #else
            static constexpr int BATCH_SIZE = 1;
        #endif
            Config(Resolver &r, qrpc_time_t st, int mbs) :
                SessionFactory::Config(r, st), max_batch_size(
                #if defined(__QRPC_USE_RECVMMSG__)
                    mbs
                #else
                    1
                #endif
                ) {}
            Config(int mbs) :
                SessionFactory::Config(SessionFactory::Config::Default()),
                max_batch_size(mbs) {}
            static inline Config Default() {
                return Config(BATCH_SIZE);
            }
        public:
            int max_batch_size;
        };
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
        class UdpSession : public Session, public IoProcessor {
        public:
            UdpSession(UdpSessionFactory &f, Fd fd, const Address &addr) : Session(f, fd, addr) { AllocIovec(); }
            DISALLOW_COPY_AND_ASSIGN(UdpSession);
            UdpSessionFactory &udp_session_factory() { return factory().to<UdpSessionFactory>(); }
            const UdpSessionFactory &udp_session_factory() const { return factory().to<UdpSessionFactory>(); }
            std::vector<struct iovec> &write_vecs() { return write_vecs_; }
            // implements Session
            const char *proto() const { return "udp"; }
            int Send(const char *data, size_t sz) override {
                return Write(data, sz);
            }
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
                            Close(sz == 0 ? QRPC_CLOSE_REASON_REMOTE : QRPC_CLOSE_REASON_LOCAL, sz);
                            break;
                        }
                    }
                }
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
                    // TODO: batched sendto for sendmmsg environment
                    struct msghdr h;
                    h.msg_name = const_cast<sockaddr *>(addr().sa());
                    h.msg_namelen = addr().salen();
                    h.msg_iov = &iov;
                    h.msg_iovlen = 1;
                    h.msg_control = nullptr;
                    h.msg_controllen = 0;
                    if (Syscall::SendTo(fd_, &h) < 0) {
                        // TODO: if EAGAIN, should try again without Reset()ing entire buffers
                        // by removing only finished iov
                        logger::error({{"ev","SendTo fails"},{"fd",fd_},{"errno",Syscall::Errno()},{"merr",Syscall::StrError()}});
                        ASSERT(false);
                        return QRPC_ESYSCALL;
                    }                    
                }
                Reset();
                return QRPC_OK;
            }
            int Write(const char *p, size_t sz) {
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
            SessionFactory(l, std::move(m), config), batch_size_(config.max_batch_size), write_buffers_(batch_size_) {}
        UdpSessionFactory(UdpSessionFactory &&rhs) : SessionFactory(std::move(rhs)),
            batch_size_(rhs.batch_size_), write_buffers_(std::move(rhs.write_buffers_)) {}
        DISALLOW_COPY_AND_ASSIGN(UdpSessionFactory);
    public:
        Fd CreateSocket(int port, bool *overflow_supported) {
            Fd fd;
            // create udp socket
            if ((fd = Syscall::CreateUDPSocket(AF_INET, overflow_supported)) < 0) {
                return INVALID_FD;
            }
            if (Syscall::Bind(fd, port) != QRPC_OK) {
                Syscall::Close(fd);
                return INVALID_FD;
            }
            return fd;
        }
    protected:
        int batch_size_;
        Allocator<WritePacketBuffer> write_buffers_;
    };
    class UdpClient : public UdpSessionFactory {
    public:
        UdpClient(
            Loop &l, Resolver &r, qrpc_time_t session_timeout = qrpc_time_sec(120),
            int batch_size = Config::BATCH_SIZE
        ) : UdpSessionFactory(l, [this](Fd fd, const Address &ap) {
            DIE("client should not call this, provide factory with SessionFactory::Connect");
            return (Session *)nullptr;
        }, Config(r, session_timeout, batch_size)) {}
        UdpClient(UdpClient &&rhs) : UdpSessionFactory(std::move(rhs)), sessions_(std::move(rhs.sessions_)) {}
        ~UdpClient() override { Fin(); }
        DISALLOW_COPY_AND_ASSIGN(UdpClient);
        void Fin() { FinSessions(sessions_); }
    public:
        // implements SessionFactory
        Session *Create(int fd, const Address &a, FactoryMethod &m) override {
            auto s = m(fd, a);
            sessions_[fd] = s;
            return s;
        }
        Session *Open(const Address &a, FactoryMethod m) override {
            bool overflow_supported;
            // use unified fd (with recv/send mmsg) or create original fd for connection.
            Fd fd = CreateSocket(0, &overflow_supported);
            auto s = Create(fd, a, m);
            if (s == nullptr) {
                ASSERT(false);
                return nullptr;
            }
            if (loop_.Add(fd, dynamic_cast<UdpSession *>(s), Loop::EV_WRITE) < 0) {
                logger::error({{"ev","Loop::Add fails"},{"fd",fd}});
                s->Close(QRPC_CLOSE_REASON_LOCAL, Syscall::Errno(), "Loop::Add fails");
                return nullptr;
            }
            return s;
        }
        void Close(Session &s) override {
            Fd fd = s.fd();
            QRPC_LOGJ(info, {{"ev","Udp::Close"},{"fd",fd},{"ps",str::dptr(&s)}});
            if (fd != INVALID_FD) {
                loop_.Del(fd);
                Syscall::Close(fd);
                sessions_.erase(fd);
            }
        }
        qrpc_time_t CheckTimeout() override { return CheckSessionTimeout(sessions_); }
    protected:
        std::map<Fd, Session*> sessions_;
    };
    class UdpListener : public UdpSessionFactory, IoProcessor {
    public:
        UdpListener(Loop &l, FactoryMethod &&m, Config c = Config::Default()) :
            UdpSessionFactory(l, std::move(m), c), fd_(INVALID_FD), port_(0),
            overflow_supported_(false), sessions_(),
            read_packets_(batch_size_), read_buffers_(batch_size_) { SetupPacket(); }
        UdpListener(UdpListener &&rhs);
        DISALLOW_COPY_AND_ASSIGN(UdpListener);
        ~UdpListener() override { Fin(); }
    public:
        Fd fd() const { return fd_; }
        int port() const { return port_; }
    public:
        void Fin() {
            FinSessions(sessions_);
            if (fd_ != INVALID_FD) {
                loop_.Del(fd_);
                Syscall::Close(fd_);
                fd_ = INVALID_FD;
            }
        }
        bool Bind() { return Listen(0); }
        bool Listen(int port) {
            if (fd_ != INVALID_FD) {
                ASSERT(port_ != 0);
                logger::warn({{"ev","already initialized"},{"fd",fd_},{"port",port_}});
                return true;
            }
            // create udp socket
            if ((fd_ = CreateSocket(port, &overflow_supported_)) < 0) {
                return false;
            }
            if (loop_.Add(fd_, this, Loop::EV_READ) < 0) {
                QRPC_LOGJ(error, {{"ev","Loop::Add fails"},{"fd",fd_}});
                Syscall::Close(fd_);
                return false;
            }
            if (port == 0) {
                if ((port_ = AssignedPort(fd_)) < 0) {
                    QRPC_LOGJ(error, {{"ev","AssignedPort() fails"},{"fd",fd_},{"r",port_}});
                    Syscall::Close(fd_);
                    fd_ = INVALID_FD;
                    return false;
                }
                logger::info({{"ev", "listen port auto assigned"},{"proto","udp"},{"port",port_}});
            } else {
                port_ = port;
            }
            return true;
        }
        int Read();
        void SetupPacket();
        void ProcessPackets(int count);
    public:
        // implements SessionFactory
        Session *Create(int fd, const Address &a, FactoryMethod &m) override {
            auto s = m(fd, a);
            sessions_[a] = s;
            return s;
        }
        Session *Open(const Address &a, FactoryMethod m) override {
            if (fd_ == INVALID_FD) {
                logger::warn({{"ev","fd not initialized: forget to call Bind() or Listen()?"}});
                ASSERT(false);
                return nullptr;
            }
            auto it = sessions_.find(a);
            if (it != sessions_.end()) {
                logger::warn({{"ev","already exists"},{"a",a.str()},{"fd",it->second->fd()}});
                ASSERT(false);
                return nullptr;
            }
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
        void Close(Session &s) override {
            sessions_.erase(s.addr());
        }
        qrpc_time_t CheckTimeout() override { return CheckSessionTimeout(sessions_); }
        // implements IoProcessor
		void OnEvent(Fd fd, const Event &e) override {
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
    protected:
        Fd fd_;
        int port_;
        bool overflow_supported_;
        std::map<Address, Session*> sessions_;
        std::vector<mmsghdr> read_packets_;
        std::vector<ReadPacketBuffer> read_buffers_;
    };
    class AdhocUdpListener : public UdpListener {
    public:
        class AdhocUdpSession : public UdpSession {
        public:
            AdhocUdpSession(AdhocUdpListener &f, Fd fd, const Address &addr) : 
                UdpSession(f, fd, addr) {}
            DISALLOW_COPY_AND_ASSIGN(AdhocUdpSession);
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
        AdhocUdpListener(AdhocUdpListener &&rhs) : UdpListener(std::move(rhs)), handler_(std::move(handler_)) {}
        DISALLOW_COPY_AND_ASSIGN(AdhocUdpListener);
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
        UdpListenerOf(UdpListenerOf &&rhs) : UdpListener(std::move(rhs)) {}
        DISALLOW_COPY_AND_ASSIGN(UdpListenerOf);
    };
    // external typedef
    typedef SessionFactory::Session Session;
    typedef TcpSessionFactory::TcpSession TcpSession;
    typedef UdpSessionFactory::UdpSession UdpSession;
}