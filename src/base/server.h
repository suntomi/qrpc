#pragma once

#include "address.h"

namespace base {
    class SessionFactory {
    public:
        class Session : public IoProcessor {
        public:
            Session(
                SessionFactory &f,
                Fd fd,
                const Address &addr,
                bool server
            ) : factory_(f), fd_(fd), addr_(addr), server_(server) {}
            Fd fd() const { return fd_; }
            const Address &addr() const { return addr_; }
            bool server() const { return server_; }
            // virtual functions to override
            virtual void Destroy() { // override function should call this
                if (fd_ != INVALID_FD) {
                    factory_.Destroy(*this);
                    fd_ = INVALID_FD;
                }
            }
            virtual bool OnConnect() { return true; }
            virtual void OnClose(bool remote) {}
            virtual bool OnRead(const char *p, size_t sz) = 0;
            // implements IoProcessor
            void OnEvent(Fd fd, const Event &e) override {
                ASSERT(fd == fd_);
                if (Loop::Readable(e)) {
                    while (true) {
                        char buffer[4096];
                        size_t sz = sizeof(buffer);
                        if ((sz = Syscall::Read(fd, buffer, sz)) < 0) {
                            if (Syscall::WriteMayBlocked(Syscall::Errno(), false)) {
                                break;
                            }
                            Destroy();
                            return;
                        }
                        if (sz == 0 || !OnRead(buffer, sz)) {
                            Destroy();
                        }
                    }
                }
            }
            void OnClose(Fd fd) override {
                ASSERT(fd == fd_);
                OnClose(true);
            }
            int OnOpen(Fd fd) override {
                ASSERT(fd == fd_);
                if (!OnConnect()) {
                    Destroy();
                }
            }
        protected:
            SessionFactory &factory_;
            Address addr_;
            Fd fd_;
            bool server_;
        };
    public:
        SessionFactory(Loop &l) : loop_(l), sessions_() {}
        Session *Open(const Address &a) {
            Fd fd = Syscall::Connect(a.sa(), a.salen());
            return Create(fd, a, false);
        }
        virtual Session *New(Fd fd, const Address &a, bool server) = 0;
    protected:
        Session *Create(Fd fd, const Address &a, bool server) {
            Session *s = New(fd, sa, salen, server);
            sessions_[fd] = s;
            if (loop_.Add(s->fd(), s, Loop::EV_READ | Loop::EV_WRITE) < 0) {
                Destroy(s);
                return nullptr;
            }
            return s;
        }
        void Destroy(Session *s) {
            Fd fd = s->fd();
            loop_.Del(fd);
            sessions_.erase(fd);
            Syscall::Close(fd);
            delete s;
        }
    protected:
        Loop &loop_;
        std::map<Fd, Session*> sessions_;
    };
    class Listener : public SessionFactory, IoProcessor {
    public:
        Listener(Loop &l) : SessionFactory(l), port_(-1), fd_(INVALID_FD) {}
        Fd fd() const { return fd_; }
        bool Listen(int port) {
            port_ = port;
            return ((fd_ = Syscall::Listen(port)) >= 0);
        }
        void Accept() {
            while (true) {
                struct sockaddr_storage sa;
                socklen_t salen = sizeof(sa);
                Fd afd = Syscall::Accept(fd_, &sa, &salen);
                if (afd < 0) {
                    if (Syscall::WriteMayBlocked(errno, false)) {
                        break;
                    }
                    return;
                }
                Create(fd, Address(sa, salen), true);
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
                Destroy(s);
            }
        }
		int OnOpen(Fd fd) {
            return QRPC_OK;
        }
    protected:
        int port_;
        Fd fd_;
    };
    template <class S, class C = S>
    class Server : public Listener {
    public:
        Server(Loop &l) : Listener(l) {}
        // implements SessionFactory
        Session *New(Fd fd, const Address &a, bool server) {
            static_assert(std::is_base_of_v<Session, S>, "S must be a descendant of Session");
            static_assert(std::is_base_of_v<Session, C>, "C must be a descendant of Session");
            return server ? new S(afd, a, server) : new C(afd, a, server);
        }
    }
}