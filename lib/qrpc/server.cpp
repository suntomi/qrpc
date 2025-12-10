#include "qrpc/server.h"
#include "qrpc/transport.h"

namespace qrpc {
  qrpc_transport_type_t Server::transport_type() const {
    return transport::type();
  }
}