#include "base/handshaker.h"
#include "base/session.h"

namespace base {
  Handshaker *Handshaker::Create(Session &s) {    
    if (s.factory().need_tls()) {
      return new TlsHandshaker(s);
    } else {
      return new PlainHandshaker(s);
    }
  }
  TlsHandshaker::TlsHandshaker(Session &s) : Handshaker() {
    // create SSL object
    ssl_ = SSL_new(dynamic_cast<TcpSession &>(s).tcp_session_factory().tls_ctx());
    if (ssl_ == nullptr) { logger::die({{"ev","SSL_new() fails"}}); }
    SSL_set_fd(ssl_, s.fd());
  }
  int TlsHandshaker::Handshake(Session &s, Fd fd, const IoProcessor::Event &ev) {
    ASSERT(ssl_ != nullptr);
    int r;
    // サーバーモードかクライアントモードか
    if (s.factory().is_listener()) {
      r = SSL_accept(ssl_);
      QRPC_LOG(info, "SSL_accept: %d", r);
    } else {
      r = SSL_connect(ssl_);
      QRPC_LOG(info, "SSL_connect: %d", r);
    }
    if (r == 1) {
      finish();
      return QRPC_OK;
    }
    int ssl_err = SSL_get_error(ssl_, r);
    if (ssl_err == SSL_ERROR_WANT_READ) {
      QRPC_LOG(info, "ssl want read");
      if ((r = s.factory().loop().Mod(fd, Loop::EV_READ)) < 0) {
        s.Close(QRPC_CLOSE_REASON_SYSCALL, r);
        return QRPC_ESYSCALL;
      }
      return QRPC_OK; // イベントループで待機
    } else if (ssl_err == SSL_ERROR_WANT_WRITE) {
      QRPC_LOG(info, "ssl want write");
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
  int TlsHandshaker::Read(Session &s, char *p, size_t sz) {
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
}