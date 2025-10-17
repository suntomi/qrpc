#include "qrpc/listener.h"
#include "qrpc/worker.h"

#include "qrpc/transport.h"

namespace qrpc {
  std::unique_ptr<Listener> Listener::Create(
      Worker &w, int port_index, const qrpc_listen_conf_t &config
  ) {
    auto l = std::make_unique<webrtc::Listener>(w, port_index, config);
    if (!l) {
      return nullptr;
    }
    l->Listen(config.port, config.ep);
    return l;
  }
} // namespace qrpc