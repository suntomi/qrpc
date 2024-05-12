#include "qrpc/listener.h"
#include "qrpc/worker.h"

#include "qrpc/transports/webrtc.h"

namespace qrpc {
  Listener *Listener::Listen(
      const Worker &w, int port_index, const qrpc_addr_t &addr, const qrpc_svconf_t &config
  ) {
    return WebRTCListener::New(w, port_index, addr, config);
  }
}