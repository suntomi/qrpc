#include "base/handshaker.h"

namespace base {
  static thread_local SSL_CTX *ctx_ = nullptr;
  class Initializer {
  public:
    Initializer() {
      ctx_ = SSL_CTX_new(TLS_method());
    }
    ~Initializer() {
      if (ctx_ != nullptr) {
        SSL_CTX_free(ctx_);
      }
    }
  };
  SSL_CTX *TlsHandshaker::ctx() {
    static Initializer init;
    ASSERT(ctx_ != nullptr);
    return ctx_;
  }
}