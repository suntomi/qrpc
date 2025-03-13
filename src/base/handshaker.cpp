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
}