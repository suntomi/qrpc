#pragma once

#include "qrpc/base.h"
#include "qrpc/handler_map.h"

#include <functional>

namespace qrpc {
  class Worker;
  class Listener {
  public:
    static std::unique_ptr<Listener> Listen(
      const Worker &w, int port_index, const qrpc_addr_t &addr, const qrpc_svconf_t &config
    );
    virtual ~Listener() = default;
  public:
    virtual HandlerMap &handler_map() = 0;
  };
} // namespace qrpc