#pragma once

#include "qrpc/base.h"
#include "qrpc/worker.h"

#include "base/conn.h"

namespace qrpc {
  class Client {
  public:
    static Client *New(const qrpc_clconf_t &conf);
    virtual ~Client() = default;
  public:
    virtual void Close(base::Connection &) = 0;
    virtual bool Connect(const qrpc_connect_conf_t &config) = 0;
    virtual void Poll() = 0;
    virtual void Close() = 0;
    virtual void Resolve(int family_pref, const std::string &host, qrpc_on_resolve_host_t cb) = 0;
    virtual void Enqueue(Worker::Task &&t) = 0;
    virtual base::Serial::PartitionId GetPartitionId() const = 0;
    virtual void ResetPartition() = 0;
    qrpc_transport_type_t transport_type() const;
    qrpc_client_t ToHandle() { return reinterpret_cast<qrpc_client_t>(this); }
    static Client *FromHandle(qrpc_client_t cl) { return reinterpret_cast<Client *>(cl); }
  };
} // namespace qrpc