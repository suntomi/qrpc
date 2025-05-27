#pragma once

#include "base/session_base.h"

namespace base {
  class Handshaker {
  public:
    typedef SessionFactory::Session Session;
    bool finished() const { return finished_; }
    virtual int Handshake(Session &s, Fd fd, const IoProcessor::Event &ev) = 0;
    virtual int Read(Session &s, char *p, size_t sz) = 0;
    virtual int Write(Session &s, const char *p, size_t sz) = 0;
    virtual int Writev(Session &s, const char *pp[], qrpc_size_t *psz, qrpc_size_t sz)  = 0;
    virtual void MigrateTo(Handshaker &hs) = 0;
    virtual bool migrated() const = 0;
    static Handshaker *Create(Session &s);
  protected:
    void finish() { finished_ = true; }
    bool finished_{false};
  };
  class PlainHandshaker : public Handshaker {
  public:
    PlainHandshaker(Session &s) : Handshaker() {}
    int Handshake(Session &s, Fd fd, const IoProcessor::Event &ev) override {
      if (Loop::Writable(ev)) {
        int r;
        if ((r = s.factory().loop().Mod(fd, Loop::EV_READ)) < 0) {
          s.Close(QRPC_CLOSE_REASON_SYSCALL, r);
          return r;
        }
        finish();
      }
      return QRPC_OK;
    }
    int Read(Session &s, char *p, size_t sz) override {
      int r = Syscall::Read(s.fd(), p, sz);
      if (r < 0) {
        int err = Syscall::Errno();
        if (Syscall::IOMayBlocked(err, false)) {
            return r;
        }
        s.Close(QRPC_CLOSE_REASON_SYSCALL, err, Syscall::StrError(err));
      }
      return r;
    }
    int Write(Session &s, const char *p, size_t sz) override {
      return Syscall::Write(s.fd(), p, sz);
    }
    int Writev(Session &s, const char *pp[], qrpc_size_t *psz, qrpc_size_t sz) override {
      return Syscall::Writev(s.fd(), pp, psz, sz);
    }
    void MigrateTo(Handshaker &hs) override {
      auto ths = dynamic_cast<Handshaker *>(&hs);
      if (ths == nullptr) {
        logger::die({{"ev", "invalid handshaker migration"}});
      }
    }
    bool migrated() const override { return true; }
  };
  class TlsHandshaker : public Handshaker {
  public:
    TlsHandshaker(Session &s);
    ~TlsHandshaker() {
      if (ssl_ != nullptr) { SSL_free(ssl_); }
    }
    SSL *ssl() { return ssl_; }
    static SSL_CTX *ctx();
    int Handshake(Session &s, Fd fd, const IoProcessor::Event &ev) override;
    int Read(Session &s, char *p, size_t sz) override;
    int Write(Session &s, const char *p, size_t sz) override {
      return SSL_write(ssl_, p, sz);
    }
    int Writev(Session &s, const char *pp[], qrpc_size_t *psz, qrpc_size_t sz) override {
      size_t tsz = 0;
      for (size_t i = 0; i < sz; i++) {
        tsz += psz[i];
      }
      char *p = (char *)Syscall::MemAlloc(tsz);
      if (p == nullptr) {
        return QRPC_EALLOC;
      }
      size_t off = 0;
      for (size_t i = 0; i < sz; i++) {
        Syscall::MemCopy(p + off, pp[i], psz[i]);
        off += psz[i];
      }
      int r = SSL_write(ssl_, p, tsz);
      Syscall::MemFree(p);
      return r;
    }
    void MigrateTo(Handshaker &hs) override {
      auto ths = dynamic_cast<TlsHandshaker *>(&hs);
      if (ths == nullptr) {
        logger::die({{"ev", "invalid handshaker migration"}});
      }
      ths->ssl_ = ssl_;
      ssl_ = nullptr;
    }
    bool migrated() const override { return ssl_ == nullptr; }
  protected:
    SSL *ssl_;
  };
}