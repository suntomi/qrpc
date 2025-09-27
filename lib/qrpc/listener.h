#pragma once

#include "qrpc/base.h"
#include "qrpc/handler_map.h"

#include <functional>

namespace qrpc {
  class Worker;
  class Listener {
  public:
    static std::unique_ptr<Listener> Listen(
      Worker &w, int port_index, const qrpc_endpoint_t &addr, const qrpc_listen_conf_t &config
    );
    virtual ~Listener() = default;
    virtual qrpc_transport_type_t transport_type() const = 0;
  public:
    virtual HandlerMap &handler_map() = 0;
  };
} // namespace qrpc