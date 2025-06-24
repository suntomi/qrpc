#pragma once

#include "base/session_base.h"
#include "base/handshaker.h"

namespace base {
    class TcpSessionFactory : public SessionFactory {
    public:
        struct Config : public SessionFactory::Config {
            Config(
                Resolver &r, qrpc_time_t session_timeout, bool is_listener, const MaybeCertPair &p
            ) : SessionFactory::Config(r, session_timeout, is_listener, p) {}
            static inline Config Default() { 
                // default no timeout
                return Config(NopResolver::Instance(), qrpc_time_sec(0), false, std::nullopt);
            }
        };
    public:
        class TcpSession : public Session, public IoProcessor {
        public:
            TcpSession(TcpSessionFactory &f, Fd fd, const Address &addr) :
                Session(f, fd, addr), handshaker_(Handshaker::Create(*this)) {}
            DISALLOW_COPY_AND_ASSIGN(TcpSession);
            inline Handshaker &hs() { return *handshaker_; }
            inline const Handshaker &hs() const { return *handshaker_; }
            inline TcpSessionFactory &tcp_session_factory() { return factory().to<TcpSessionFactory>(); }
            inline void MigrateTo(TcpSession *newsession) {
                ASSERT(this != newsession && newsession != nullptr && newsession->fd() == fd_);
                factory().loop().ModProcessor(fd_, newsession);
                fd_ = INVALID_FD; // invalidate fd_ so that SessionFactory::Close will not close fd_
                hs().MigrateTo(newsession->hs());
                tcp_session_factory().UpdateSession(*newsession);
            }
            inline bool migrated() const { return fd_ == INVALID_FD && hs().migrated(); }
            inline int Write(const char *p, size_t sz) { return hs().Write(*this, p, sz); }
            inline int Writev(const char *pp[], size_t *psz, size_t sz) { return hs().Writev(*this, pp, psz, sz); }
            inline int Read(char *p, size_t sz) { return hs().Read(*this, p, sz); }
            // implements Session
            const char *proto() const override { return "TCP"; }
            // implements IoProcessor
            void OnEvent(Fd fd, const Event &e) override {
                ASSERT(fd == fd_);
                int r;
                if (!hs().finished()) {
                    if ((r = hs().Handshake(*this, fd, e)) < 0) {
                        return; // handshake is not finished yet, go on (or closed)
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
                        if ((sz = Read(buffer, sz)) < 0) {
                            break; // close may called in each handshaker
                        }
                        if (sz == 0 || (sz = OnRead(buffer, sz)) < 0) {
                            Close((migrated() ? QRPC_CLOSE_REASON_MIGRATED :
                                (sz == 0 ? QRPC_CLOSE_REASON_REMOTE : QRPC_CLOSE_REASON_LOCAL)), sz);
                            break;
                        }
                    }
                }
            }
        protected:
            Handshaker *handshaker_;
        };
    public:
        TcpSessionFactory(Loop &l, FactoryMethod &&m, Config c = Config::Default()) :
            SessionFactory(l, std::move(m), c) {}
        TcpSessionFactory(TcpSessionFactory &&rhs) :
            SessionFactory(std::move(rhs)), sessions_(std::move(rhs.sessions_)) {
            tls_ctx_ = rhs.tls_ctx_;
            rhs.tls_ctx_ = nullptr;
        }
        ~TcpSessionFactory() override { Fin(); }
        DISALLOW_COPY_AND_ASSIGN(TcpSessionFactory);
        void Fin() { FinSessions(sessions_); }
        // implements SessionFactory
        Session *Open(const Address &a, FactoryMethod m) override {
            Fd fd = Syscall::Connect(a.sa(), a.salen());
            if (fd == INVALID_FD) {
                return nullptr;
            }
            auto s = dynamic_cast<TcpSession *>(Create(fd, a, m));
            int r; // EV_WRITE only for waiting for establishing connection
            if ((r = loop_.Add(s->fd(), s, Loop::EV_WRITE)) < 0) {
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
        void UpdateSession(Session &s) {
            sessions_[s.fd()] = &s;
        }
        qrpc_time_t CheckTimeout() override { return CheckSessionTimeout(sessions_); }
    protected:
        // deletion timing of Session* is severe, so we want to have full control of it.
        std::map<Fd, Session*> sessions_;
    };
    class TcpClient : public TcpSessionFactory {
    public:
        struct Config : public TcpSessionFactory::Config {
            Config(Resolver &r, qrpc_time_t st, const MaybeCertPair p) :
                TcpSessionFactory::Config(r, st, false, p) {}
        };
    public:
        TcpClient(
            Loop &l, Resolver &r, qrpc_time_t timeout = qrpc_time_sec(120),
            const MaybeCertPair &p = std::nullopt
        ) : TcpSessionFactory(l, [](Fd fd, const Address &a) -> Session* {
            DIE("client should not call this, provide factory via SessionFactory::Connect");
            return (Session *)nullptr;
        }, Config(r, timeout, p)) {}
        TcpClient(TcpClient &&rhs) : TcpSessionFactory(std::move(rhs)) {}
        DISALLOW_COPY_AND_ASSIGN(TcpClient);
    };
    class TcpListener : public TcpSessionFactory, public IoProcessor {
    public:
        struct Config : public TcpSessionFactory::Config {
            Config(Resolver &r, qrpc_time_t st, const MaybeCertPair &p = std::nullopt) :
                TcpSessionFactory::Config(r, st, true, p) {}
            static inline Config Default() { 
                // default no timeout
                return Config(NopResolver::Instance(), qrpc_time_sec(0));
            }
        };
    public:
        TcpListener(Loop &l, FactoryMethod &&m, Config c = Config::Default()) : 
            TcpSessionFactory(l, std::move(m), c) {}
        TcpListener(TcpListener &&rhs) : TcpSessionFactory(std::move(rhs)), 
            fd_(rhs.fd_), port_(rhs.port_) { rhs.fd_ = INVALID_FD; }
        ~TcpListener() override { Fin(); }
        DISALLOW_COPY_AND_ASSIGN(TcpListener);
        Fd fd() const { return fd_; }
        int port() const { return port_; }
        void Fin() {
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
            ASSERT(is_listener());
            while (true) {
                struct sockaddr_storage sa;
                socklen_t salen = sizeof(sa);
                Fd afd = Syscall::Accept(fd_, sa, salen);
                if (afd < 0) {
                    if (Syscall::IOMayBlocked(Syscall::Errno(), false)) {
                        break;
                    }
                    return;
                }
                auto a = Address(sa, salen);
                logger::info({{"ev","accept"},{"proto","tcp"},{"lfd",fd_},{"fd",afd},{"a",a.str()}});
                auto s = dynamic_cast<TcpSession *>(Create(afd, a, factory_method_));
                int r;
                if ((r = loop_.Add(s->fd(), s, Loop::EV_READ)) < 0) {
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
        Fd fd_{INVALID_FD};
        int port_{0};
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
            Config(Resolver &r, qrpc_time_t st, int mbs, bool sw, bool is_listener) :
                SessionFactory::Config(r, st, is_listener), max_batch_size(
                #if defined(__QRPC_USE_RECVMMSG__)
                    mbs
                #else
                    1
                #endif
                ), stream_write(sw) {}
            Config(int mbs, bool sw, bool is_listener) :
                Config(NopResolver::Instance(), qrpc_time_sec(0), mbs, sw, is_listener) {}
            static inline Config Default() {
                return Config(BATCH_SIZE, false, false);
            }
        public:
            int max_batch_size{BATCH_SIZE};
            bool stream_write{false};
        };
        #if !defined(__QRPC_USE_RECVMMSG__)
        struct mmsghdr {
            struct msghdr msg_hdr;
            unsigned int msg_len;
        };
        #endif
        struct ReadPacketBuffer {
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
            UdpSession(UdpSessionFactory &f, Fd fd, const Address &addr) : Session(f, fd, addr) {}
            ~UdpSession() override { FreeIovecs(); }
            DISALLOW_COPY_AND_ASSIGN(UdpSession);
            UdpSessionFactory &udp_session_factory() { return factory().to<UdpSessionFactory>(); }
            const UdpSessionFactory &udp_session_factory() const { return factory().to<UdpSessionFactory>(); }
            std::vector<struct iovec> &write_vecs() { return write_vecs_; }
            int Flush(); 
            void Reset(size_t size) {
                ASSERT(size > 0);
                if (size >= write_vecs_.size()) {
                    for (int i = 0; i < ((int)size) - 1; i++) {
                        struct iovec &iov = write_vecs_.back();
                        FreeIovec(iov);
                        write_vecs_.pop_back();
                    }
                    // remain first buffer for next write
                    auto &iov = write_vecs_[0];
                    iov.iov_len = 0;
                } else if (size > 0) {
                    for (size_t i = 0; i < size; i++) {
                        struct iovec &iov = write_vecs_[i];
                        FreeIovec(iov);
                    }
                    write_vecs_.erase(write_vecs_.begin(), write_vecs_.begin() + size);
                }
            }            
            // implements Session
            const char *proto() const override { return "UDP"; }
            // Send is implemented in subclass
        protected:
            bool AllocIovec(size_t sz) {
                void *b;
                if (sz > Syscall::kMaxOutgoingPacketSize) {
                    QRPC_LOGJ(warn, {{"ev","try to send packet lager than MTU"},{"sz",sz}});
                    b = Syscall::MemAlloc(sz);
                } else {
                    b = udp_session_factory().write_buffers_.Alloc();
                }
                if (b == nullptr) {
                    logger::error({{"ev","write_buffers_.Alloc fails"},{"sz",sz}});
                    ASSERT(false);
                    return false;
                }
                write_vecs_.push_back({ .iov_base = b, .iov_len = 0 });
                return true;
            }
            void FreeIovec(struct iovec &iov) {
                if (iov.iov_len > Syscall::kMaxOutgoingPacketSize) {
                    Syscall::MemFree(iov.iov_base);
                } else {
                    udp_session_factory().write_buffers_.Free(iov.iov_base);
                }
            }
            void FreeIovecs() {
                for (size_t i = 0; i < write_vecs_.size(); i++) {
                    struct iovec &iov = write_vecs_[i];
                    FreeIovec(iov);
                }
                write_vecs_.clear();
            }
            int Write(const char *p, size_t sz) {
                if (!AllocIovec(sz)) {
                    ASSERT(false);
                    return QRPC_EALLOC;
                }
                struct iovec &curr_iov = write_vecs_[write_vecs_.size() - 1];
                ASSERT(curr_iov.iov_len == 0);
                Syscall::MemCopy(reinterpret_cast<char *>(curr_iov.iov_base), p, sz);
                curr_iov.iov_len = sz;
                return QRPC_OK;
            }
        private:
            std::vector<struct iovec> write_vecs_;
        };
        class Flusher {
        public:
            template <class C>
            static inline void Try(C &c, AlarmProcessor &ap) {
                if (c.Flush() > 0) {
                    Start(c, ap);
                }
            }
            template <class C>
            static inline void Start(C &c, AlarmProcessor &ap) {
                if (c.alarm_id_ != AlarmProcessor::INVALID_ID) {
                    return;
                }
                c.alarm_id_ = ap.Set([&c]() {
                    if (c.Flush() > 0) {
                        return qrpc_time_now() + qrpc_time_usec(100);
                    } else {
                        c.alarm_id_ = AlarmProcessor::INVALID_ID;
                        return qrpc_alarm_stop_rv();
                    }
                }, qrpc_time_now() + qrpc_time_usec(100));
            }
        };
    public:
        UdpSessionFactory(Loop &l, FactoryMethod &&m, Config config = Config::Default()) :
            SessionFactory(l, std::move(m), config), batch_size_(config.max_batch_size),
            stream_write_(config.stream_write), write_buffers_(batch_size_) {}
        UdpSessionFactory(UdpSessionFactory &&rhs) : SessionFactory(std::move(rhs)),
            batch_size_(rhs.batch_size_), stream_write_(rhs.stream_write_),
            write_buffers_(std::move(rhs.write_buffers_)) {}
        ~UdpSessionFactory() override {}
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
        bool stream_write_;
        Allocator<WritePacketBuffer> write_buffers_;
    };
    class UdpClient : public UdpSessionFactory {
    public:
        class UdpSession : public UdpSessionFactory::UdpSession, public IoProcessor {
        public:
            friend class Flusher;
            UdpSession(UdpSessionFactory &f, Fd fd, const Address &addr) :
                UdpSessionFactory::UdpSession(f, fd, addr) {}
            ~UdpSession() override {
                if (alarm_id_ != AlarmProcessor::INVALID_ID) {
                    udp_session_factory().alarm_processor().Cancel(alarm_id_);
                }
            }
            // implements Session
            int Send(const char *data, size_t sz) override {
                int r;
                if ((r = Write(data, sz)) < 0) {
                    QRPC_LOGJ(error, {{"ev","UdpSession::Write fails"},{"fd",fd_},{"sz",sz},{"r",r}});
                    return r;
                }
                StartFlushTask();
                return r;
            }
            // implements IoProcessor
            void OnEvent(Fd fd, const Event &e) override {
                ASSERT(fd == fd_);
                if (Loop::Writable(e)) {
                    int r;
                    if ((r = factory().loop().Mod(fd, Loop::EV_READ)) < 0) {
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
                            break;
                        }
                        if (sz == 0 || (sz = OnRead(buffer, sz)) < 0) {
                            TryFlush();
                            Close(sz == 0 ? QRPC_CLOSE_REASON_REMOTE : QRPC_CLOSE_REASON_LOCAL, sz);
                            return;
                        }
                    }
                    TryFlush();
                }
            }            
        protected:
            inline void TryFlush() { Flusher::Try(*this, udp_session_factory().alarm_processor()); }
            inline void StartFlushTask() { Flusher::Start(*this, udp_session_factory().alarm_processor()); }
        protected:
            AlarmProcessor::Id alarm_id_{AlarmProcessor::INVALID_ID};
        };
    public:
        UdpClient(
            Loop &l, Resolver &r, qrpc_time_t session_timeout = qrpc_time_sec(120),
            int batch_size = Config::BATCH_SIZE, bool stream_write = false
        ) : UdpSessionFactory(l, [](Fd fd, const Address &ap) {
            DIE("client should not call this, provide factory with SessionFactory::Connect");
            return (Session *)nullptr;
        }, Config(r, session_timeout, batch_size, stream_write, false)) {}
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
            // create original fd for connection.
            // because client session factory need multiple connection to same destination
            Fd fd = CreateSocket(0, &overflow_supported);
            auto s = dynamic_cast<UdpSession *>(Create(fd, a, m));
            if (s == nullptr) {
                ASSERT(false);
                return nullptr;
            }
            if (loop_.Add(fd, s, Loop::EV_WRITE) < 0) {
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
        friend class Flusher;
        struct Config : public UdpSessionFactory::Config {
            Config(Resolver &r, qrpc_time_t st, int mbs, bool sw) : UdpSessionFactory::Config(r, st, mbs, sw, true) {}
            static inline Config Default() { 
                // default no timeout
                return Config(NopResolver::Instance(), qrpc_time_sec(0), BATCH_SIZE, false);
            }
        };
    public:
        class UdpSession : public UdpSessionFactory::UdpSession {
        public:
            UdpSession(UdpSessionFactory &f, Fd fd, const Address &addr) :
                UdpSessionFactory::UdpSession(f, fd, addr) {}
            // implements Session
            int Send(const char *data, size_t sz) override {
                int r;
                if ((r = Write(data, sz)) < 0) {
                    QRPC_LOGJ(error, {{"ev","UdpSession::Write fails"},{"fd",fd_},{"sz",sz},{"r",r}});
                    return r;
                }
                factory().to<UdpListener>().StartFlushTask();
                return r;
            }
        };
    public:
        UdpListener(Loop &l, FactoryMethod &&m, Config c = Config::Default()) :
            UdpSessionFactory(l, std::move(m), c),
            read_packets_(batch_size_), read_buffers_(batch_size_) { Init(); }
        UdpListener(UdpListener &&rhs);
        ~UdpListener() override { Fin(); }
        DISALLOW_COPY_AND_ASSIGN(UdpListener);
    public:
        Fd fd() const { return fd_; }
        int port() const { return port_; }
    public:
        void Init() { SetupPacket(); }
        void Fin() {
            FinSessions(sessions_);
            if (fd_ != INVALID_FD) {
                loop_.Del(fd_);
                Syscall::Close(fd_);
                fd_ = INVALID_FD;
            }
            if (alarm_id_ != AlarmProcessor::INVALID_ID) {
                alarm_processor().Cancel(alarm_id_);
                alarm_id_ = AlarmProcessor::INVALID_ID;
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
        int Flush();
    protected:
        inline void TryFlush() { Flusher::Try(*this, alarm_processor()); }
        inline void StartFlushTask() { Flusher::Start(*this, alarm_processor()); }
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
        Fd fd_{INVALID_FD};
        int port_{0};
        bool overflow_supported_{false};
        AlarmProcessor::Id alarm_id_{AlarmProcessor::INVALID_ID};
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
        AdhocUdpListener(AdhocUdpListener &&rhs) : UdpListener(std::move(rhs)), handler_(std::move(rhs.handler_)) {}
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
            static_assert(std::is_base_of<UdpSession, S>(), "S must be a descendant of UdpSession");
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