#include "qrpc/listener.h"
#include "qrpc/worker.h"

#include "qrpc/transport.h"

namespace qrpc {
  std::unique_ptr<Listener> Listener::Listen(
      Worker &w, int port_index, const qrpc_addr_t &addr, const qrpc_svconf_t &config
  ) {
    return std::make_unique<webrtc::Listener>(w, port_index, addr, config);
  }
} // namespace qrpc