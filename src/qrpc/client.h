#pragma once

#include "qrpc/base.h"
#include "qrpc/handler_map.h"

namespace qrpc {
  class Client {
  public:
    static Client *New(int max_nfd, int max_stream_hint, const qrpc_dns_conf_t &dns_conf);
    virtual ~Client() = default;
  public:
    virtual HandlerMap &handler_map() = 0;
    virtual void Close(Connection &) = 0;
    virtual bool Connect(
      const std::string &host, int port, const qrpc_clconf_t &config
    ) = 0;
  };
} // namespace qrpc