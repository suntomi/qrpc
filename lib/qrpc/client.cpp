#include "qrpc/client.h"
#include "qrpc/transport.h"

namespace qrpc {
  Client *Client::New(const qrpc_clconf_t &conf) {
    // TODO: support other type of transports
    auto c = new webrtc::Client(conf);
    return dynamic_cast<Client *>(c);
  }
}