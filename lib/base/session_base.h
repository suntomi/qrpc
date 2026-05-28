#pragma once

#include "base/address.h"
#include "base/alarm.h"
#include "base/allocator.h"
#include "base/crypto.h"
#include "base/loop.h"
#include "base/io_processor.h"
#include "base/macros.h"
#include "base/type.h"
#include "base/resolver.h"

#include <openssl/ssl.h>
#include <openssl/err.h>

#include <functional>
#include <optional>

namespace base {
    DISABLE_MAYBE_UNINITIALIZED_WARNING_PUSH    
    class CertificatePair {
    public:
        std::string cert, privkey;
        std::vector<std::string> hostnames;
    public:
        bool empty() const {
          return (!Syscall::FileExists(cert) || !Syscall::FileExists(privkey)) && hostnames.empty();
        }
        bool need_autogen() const { return hostnames.size() > 0; }
        std::string TryAutoGen();
        static inline CertificatePair Default() { return CertificatePair(); }
    };
    typedef std::optional<CertificatePair> MaybeCertPair;
    struct ClientConnectOptions {
        bool use_tls{false};
        MaybeCertPair certpair{std::nullopt};
    };
    DISABLE_MAYBE_UNINITIALIZED_WARNING_POP
    class SessionFactory {
    public:
        struct Config {
            explicit Config(qrpc_time_t st) : session_timeout(st) {}
            static inline Config Default() { 
                // default no timeout
                return Config(qrpc_time_sec(0));
            }
        public:
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
            // Instead, return negative value from them to close, or call Shutdown(...)
            inline bool Close(qrpc_close_reason_code_t code,
                int64_t detail_code = 0, const std::string &msg = "") {
                return Close({ .code = code, .detail_code = detail_code, .msg = msg });
            }
            // this closes session and safely callable from OnRead(), OnConnect()
            // but recommended to return negative value from them to close. use this only when there is no other way.
            inline bool Shutdown(qrpc_close_reason_code_t code,
                int64_t detail_code = 0, const std::string &msg = "") {
                // set close reason first so that cancel the alarm when session is deleted before alarm raised
                SetCloseReason({ .code = code, .detail_code = detail_code, .msg = msg });
                return (this->close_reason_->alarm_id = factory().alarm_processor().Set(
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
                    }, qrpc_time_now() // call alarm handler asap
                )) != AlarmProcessor::INVALID_ID;
            }
            inline void Touch(qrpc_time_t at) { last_active_ = at; }
            // virtual functions to override
            virtual int Send(const char *data, size_t sz) {
                return Syscall::Write(fd_, data, sz);
            }
            virtual int Write(const char *data, size_t sz) {
                return Syscall::Write(fd_, data, sz);
            }
            virtual int Read(char *p, size_t sz) {
                return Syscall::Read(fd_, p, sz);
            }
            virtual bool is_listener() const { return factory_.is_listener(); }
            virtual bool need_tls() const { return false; }
            virtual SSL_CTX *tls_ctx() const { return nullptr; }
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
                    logger::info({ // it is possible that fd_ is invalid_fd at here, because of migration
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
            loop_(l), alarm_processor_(l.alarm_processor()),
            factory_method_(m) { Init(); }
        SessionFactory(Loop &l, FactoryMethod &&m, Config c) :
            loop_(l), alarm_processor_(l.alarm_processor()),
            factory_method_(m), session_timeout_(c.session_timeout) { Init(); }
        SessionFactory(SessionFactory &&rhs);
        virtual ~SessionFactory() noexcept { Fin(); }
        DISALLOW_COPY_AND_ASSIGN(SessionFactory);
    public:
        inline Loop &loop() { return loop_; }
        inline AlarmProcessor &alarm_processor() { return alarm_processor_; }
        inline qrpc_time_t session_timeout() const { return session_timeout_; }
        virtual bool is_listener() const { return false; }
        template <class F> F &to() {return base::type::cast_or_die<F>(*this); }
        template <class F> const F &to() const { return base::type::cast_or_die<const F>(*this); }
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
        void Init();
        void Fin();
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
        }
    public:
        virtual Session *Open(const Address &a, FactoryMethod m) = 0;
        virtual void OnSessionMigrated(Session &s) {}
    protected:
        virtual Session *Create(int fd, const Address &a, FactoryMethod &m) = 0;
        virtual void Close(Session &s) = 0;
        virtual qrpc_time_t CheckTimeout() = 0;
    protected:
        Loop &loop_;
        AlarmProcessor &alarm_processor_;
        FactoryMethod factory_method_;
        AlarmProcessor::Id alarm_id_{AlarmProcessor::INVALID_ID};
        qrpc_time_t session_timeout_{0ULL};
    };
    class ClientSessionFactory : public SessionFactory {
    public:
        using ConnectOptions = ClientConnectOptions;
        struct Config : public SessionFactory::Config {
            Config(Resolver &r, qrpc_time_t st) :
                SessionFactory::Config(st), resolver(r) {}
            static inline Config Default() {
                return Config(NopResolver::Instance(), qrpc_time_sec(0));
            }
        public:
            Resolver &resolver;
        };
        class Session : public SessionFactory::Session {
        public:
            typedef ClientSessionFactory Factory;
            Session(SessionFactory &f, Fd fd, const Address &addr) :
                SessionFactory::Session(f, fd, addr) {}
            ~Session() override { FinTlsContext(); }
            void ApplyConnectOptions(const ConnectOptions &opts) {
                use_tls_ = opts.use_tls;
                certpair_ = opts.certpair;
                if (use_tls_) { EnsureTlsContext(); }
            }
            bool need_tls() const override { return use_tls_; }
            SSL_CTX *tls_ctx() const override { return tls_ctx_; }
        protected:
            void EnsureTlsContext();
            void FinTlsContext();
        protected:
            bool use_tls_{false};
            MaybeCertPair certpair_{std::nullopt};
            SSL_CTX *tls_ctx_{nullptr};
        };
    public:
        ClientSessionFactory(Loop &l, FactoryMethod &&m, Config c = Config::Default()) :
            SessionFactory(l, std::move(m), SessionFactory::Config(c.session_timeout)),
            resolver_(c.resolver) {}
        ClientSessionFactory(ClientSessionFactory &&rhs);
        ~ClientSessionFactory() noexcept override = default;
        bool Connect(const std::string &host, int port, FactoryMethod m, DnsErrorHandler eh, int family_pref = AF_INET);
        bool Connect(const std::string &host, int port, FactoryMethod m, int family_pref = AF_INET);
        bool Connect(const std::string &host, int port, FactoryMethod m, const ConnectOptions &opts,
            DnsErrorHandler eh, int family_pref = AF_INET);
        bool Connect(const std::string &host, int port, FactoryMethod m, const ConnectOptions &opts,
            int family_pref = AF_INET);
    protected:
        Resolver &resolver_;
    };
    class ListenerSessionFactory : public SessionFactory {
    public:
        struct Config : public SessionFactory::Config {
            Config(qrpc_time_t st, const MaybeCertPair &p = std::nullopt) :
                SessionFactory::Config(st), certpair(p) {}
            static inline Config Default() {
                return Config(qrpc_time_sec(0));
            }
        public:
            MaybeCertPair certpair;
        };
    public:
        class Session : public SessionFactory::Session {
        public:
            typedef ListenerSessionFactory Factory;
            Session(SessionFactory &f, Fd fd, const Address &addr) :
                SessionFactory::Session(f, fd, addr) {}
            bool is_listener() const override { return true; }
            bool need_tls() const override {
                return factory().to<ListenerSessionFactory>().need_tls();
            }
            SSL_CTX *tls_ctx() const override {
                return factory().to<ListenerSessionFactory>().tls_ctx();
            }
        };
    public:
        ListenerSessionFactory(Loop &l, FactoryMethod &&m, Config c = Config::Default()) :
            SessionFactory(l, std::move(m), SessionFactory::Config(c.session_timeout)),
            certpair_(c.certpair) {
            if (need_tls()) { EnsureTlsContext(); }
        }
        ListenerSessionFactory(ListenerSessionFactory &&rhs);
        ~ListenerSessionFactory() noexcept override { FinTlsContext(); }
        bool is_listener() const override { return true; }
        bool need_tls() const { return certpair_.has_value(); }
        SSL_CTX *tls_ctx() const { return tls_ctx_; }
    protected:
        void EnsureTlsContext();
        void FinTlsContext();
        MaybeCertPair certpair_;
        SSL_CTX *tls_ctx_{nullptr};
    };
  } // namespace base
