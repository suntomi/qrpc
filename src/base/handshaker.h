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
      return Syscall::Read(s.fd(), p, sz);
    }
    int Write(Session &s, const char *p, size_t sz) override {
      return Syscall::Write(s.fd(), p, sz);
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
    int Handshake(Session &s, Fd fd, const IoProcessor::Event &ev) override {
      ASSERT(ssl_ != nullptr);
      int r;
      // サーバーモードかクライアントモードか
      if (s.factory().is_listener()) {
        r = SSL_accept(ssl_);
      } else {
        r = SSL_connect(ssl_);
      }
      if (r == 1) {
        finish();
        return QRPC_OK;
      }
      int ssl_err = SSL_get_error(ssl_, r);
      if (ssl_err == SSL_ERROR_WANT_READ) {
        if ((r = s.factory().loop().Mod(fd, Loop::EV_READ)) < 0) {
          s.Close(QRPC_CLOSE_REASON_SYSCALL, r);
          return QRPC_ESYSCALL;
        }
        return QRPC_OK; // イベントループで待機
      } else if (ssl_err == SSL_ERROR_WANT_WRITE) {
        if ((r = s.factory().loop().Mod(fd, Loop::EV_WRITE)) < 0) {
          s.Close(QRPC_CLOSE_REASON_SYSCALL, r);
          return QRPC_ESYSCALL;
        }
        return QRPC_OK; // イベントループで待機
      } else {
        char err_buf[4096];
        ERR_error_string_n(ERR_get_error(), err_buf, sizeof(err_buf));
        QRPC_LOGJ(error, {{"ev", "SSL handshake error"}, {"code", ssl_err}, {"err", err_buf}});
        s.Close(QRPC_CLOSE_REASON_PROTOCOL, ssl_err, err_buf);
        return QRPC_ESYSCALL;
      } 
    }
    int Read(Session &s, char *p, size_t sz) override {
      ASSERT(ssl_ != nullptr);
      int r = SSL_read(ssl_, p, sz);
      if (r > 0) {
        return r; // 読み込み成功
      }
      int ssl_err = SSL_get_error(ssl_, r);
      if (ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE) {
        return QRPC_EAGAIN;
      } else {
        char err_buf[4096];
        ERR_error_string_n(ERR_get_error(), err_buf, sizeof(err_buf));
        QRPC_LOGJ(error, {{"ev", "SSL read error"}, {"code", ssl_err}, {"err", err_buf}});
        s.Close(QRPC_CLOSE_REASON_PROTOCOL, ssl_err, err_buf);
        return QRPC_ESYSCALL;
      }        
    }
    int Write(Session &s, const char *p, size_t sz) override {
      return SSL_write(ssl_, p, sz);
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