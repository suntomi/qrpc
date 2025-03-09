#pragma once

#include "base/session_base.h"

#include <openssl/ssl.h>
#include <openssl/err.h>

namespace base {
  class Handshaker {
  public:
    typedef SessionFactory::Session Session;
    bool finished() const { return finished_; }
    void finish() { finished_ = true; }
  protected:
    bool finished_{false};
  };
  class PlainHandshaker : public Handshaker {
  public:
      PlainHandshaker(Session &s) : Handshaker() {}
      int Handshake(Session &s, Fd fd, const IoProcessor::Event &ev) {
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
      int Read(Session &s, char *p, size_t sz) {
        if ((sz = Syscall::Read(s.fd(), p, sz)) < 0) {
          int err = Syscall::Errno();
          if (Syscall::IOMayBlocked(err, false)) {
            return;
          }
          s.Close(QRPC_CLOSE_REASON_SYSCALL, err, Syscall::StrError(err));
        }
        return sz;
      }
    };
    class TlsHandshaker : public Handshaker {
    public:
      TlsHandshaker(Session &s) : Handshaker() {
        // create SSL object
        ssl_ = SSL_new(ctx());
        if (ssl_ == nullptr) { logger::die({{"ev","SSL_new() fails"}}); }
        SSL_set_fd(ssl_, s.fd());
      }
      ~TlsHandshaker() {
        if (ssl_ != nullptr) { SSL_free(ssl_); }
      }
      SSL_CTX *ctx();
      int Handshake(Session &s, Fd fd, const IoProcessor::Event &ev) {
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
      int Read(Session &s, char *p, size_t sz) {
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
    protected:
      SSL *ssl_;
    };  
}