#pragma once

#include "qrpc/base.h"

#include <functional>

namespace qrpc {
  class Worker;
  class Listener {
  public:
    static std::unique_ptr<Listener> Create(
      Worker &w, int port_index, const qrpc_listen_conf_t &config
    );
    virtual ~Listener() = default;
    virtual qrpc_transport_type_t transport_type() const = 0;
  };
} // namespace qrpc