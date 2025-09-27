#include "qrpc/listener.h"
#include "qrpc/worker.h"

#include "qrpc/transport.h"

namespace qrpc {
  std::unique_ptr<Listener> Listener::Listen(
      Worker &w, int port_index, const qrpc_endpoint_t &addr, const qrpc_listen_conf_t &config
  ) {
    return std::make_unique<webrtc::Listener>(w, port_index, addr, config);
  }
} // namespace qrpc