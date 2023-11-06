#pragma once

#include "base/address.h"
#include "base/loop.h"
#include "base/io_processor.h"
#include "base/macros.h"

#include <functional>

namespace base {
    class SessionFactory {
    public:
        class Session : public IoProcessor {
        public:
            struct CloseReason {
                qrpc_close_reason_code_t code;
                int64_t detail_code;
                std::string msg;
            };
        public:
            Session(SessionFactory &f, Fd fd, const Address &addr) : 
                factory_(f), fd_(fd), addr_(addr), close_reason_() {}
            ~Session() {}
            inline Fd fd() const { return fd_; }
            inline SessionFactory &factory() { return factory_; }
            inline const Address &addr() const { return addr_; }
            inline bool closed() const { return close_reason_ != nullptr; }
            inline void Close(
                qrpc_close_reason_code_t code, int64_t detail_code = 0, const std::string &msg = ""
            ) {
                Close( { .code = code, .detail_code = detail_code, .msg = msg });
            }
            inline void MigrateTo(Session *newsession) {
                factory_.loop().ModProcessor(fd_, newsession);
                // close this session without closing fd
                SetCloseReason({ .code = QRPC_CLOSE_REASON_MIGRATED, .detail_code = 0, .msg = "" });
            }
            // virtual functions to override
            virtual void Close(const CloseReason &reason) {
                if (!closed()) {
                    logger::info({
                        {"msg", "close"},{"fd",fd()},{"addr", addr().str()},
                        {"rc", reason.code},{"dc", reason.detail_code},{"rmsg", reason.msg}
                    });
                    SetCloseReason(reason);
                    factory_.Close(this);
                }
            }
            virtual int Send(const char *data, size_t sz) {
                return Syscall::Write(fd_, data, sz);
            }
            virtual int OnConnect() { return QRPC_OK; }
            virtual void OnClose() {}
            virtual int OnRead(const char *p, size_t sz) = 0;
            // implements IoProcessor
            void OnEvent(Fd fd, const Event &e) override {
                ASSERT(fd == fd_);
                if (Loop::Readable(e)) {
                    while (LIKELY(!closed())) {
                        char buffer[4096];
                        size_t sz = sizeof(buffer);
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
                OnClose();
            }
            int OnOpen(Fd fd) override {
                ASSERT(fd == fd_);
                int r;
                if ((r = OnConnect()) < 0) {
                    return r;
                }
                return QRPC_OK;
            }
        private:
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
        typedef std::function<Session* (Fd, const Address &)> FactoryMethod;
    public:
        SessionFactory(Loop &l) : loop_(l), sessions_() {}
        virtual ~SessionFactory() {}
        template <class S> S &server() { return static_cast<S&>(*this); }
        template <class S> const S &server() const { return static_cast<const S&>(*this); }
        Loop &loop() { return loop_; }
        Session *Open(const Address &a, FactoryMethod m) {
            Fd fd = Syscall::Connect(a.sa(), a.salen());
            return Create(fd, a, m);
        }
        bool Resolve(int family_pref, const std::string &host, int port, FactoryMethod m);
    protected:
        Session *Create(Fd fd, const Address &a, FactoryMethod &m) {
            Session *s = m(fd, a);
            sessions_[fd] = s;
            if (loop_.Add(s->fd(), s, Loop::EV_READ | Loop::EV_WRITE) < 0) {
                Close(s);
                return nullptr;
            }
            return s;
        }
        void Close(Session *s) {
            Fd fd = s->fd();
            if (fd != INVALID_FD) {
                sessions_.erase(fd);
                loop_.Del(fd);
                Syscall::Close(fd);
            }
        }
    protected:
        Loop &loop_;
        std::map<Fd, Session*> sessions_;
    };
    class Listener : public SessionFactory, public IoProcessor {
    public:
        Listener(Loop &l, FactoryMethod m) : 
            SessionFactory(l), port_(-1), fd_(INVALID_FD), factory_method_(m) {}
        Fd fd() const { return fd_; }
        bool Listen(int port) {
            port_ = port;
            if ((fd_ = Syscall::Listen(port)) < 0) {
                logger::error({{"msg","Syscall::Listen() fails"},{"port",port},{"errno",Syscall::Errno()}});
                return fd_;
            }
            if (loop_.Add(fd_, this, Loop::EV_READ) < 0) {
                logger::error({{"msg","Loop::Add fails"},{"fd",fd_}});
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
                logger::info({{"msg", "accept"},{"afd",afd},{"fd",fd_},{"addr", a.str()}});
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
    protected:
        int port_;
        Fd fd_;
        FactoryMethod factory_method_;
    };
    template <class S>
    class Server : public Listener {
    public:
        Server(Loop &l, FactoryMethod m) : Listener(l, m) {}
        Server(Loop &l) : Listener(l, [this](Fd fd, const Address &a) {
            return new S(*this, fd, a);
        }) {}
    };
    // external typedef
    typedef SessionFactory::Session Session;
}