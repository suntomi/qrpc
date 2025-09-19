#include "qrpc/client.h"
#include "qrpc/transport.h"

namespace qrpc {
  Client *Client::New(const qrpc_clconf_t &conf) {
    auto c = new transport::Client(conf);
    return dynamic_cast<Client *>(c);
  }
}