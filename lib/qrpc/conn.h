#pragma once

#include "base/conn.h"
#include "base/serial.h"

namespace qrpc {
class Connection : public virtual base::Connection {
public:
  virtual ~Connection() = default;
public:
  virtual const qrpc_serial_t &serial() const = 0;
  qrpc_conn_t ToHandle() const { return { .s = serial(), .p = const_cast<Connection *>(this) }; }
  static Connection *FromHandle(qrpc_conn_t conn) {
    auto p = reinterpret_cast<const Connection *>(conn.p);
    if (p == nullptr || !base::Serial::IsSame(p->serial(), conn.s)) {
      return nullptr;
    }
    return const_cast<Connection *>(p);
  }
};

template <class BaseConnectionT>
class ConnectionImplT : public BaseConnectionT, public qrpc::Connection {
public:
  template <class... Args>
  ConnectionImplT(base::Serial::PartitionId pid, Args&&... args) :
    BaseConnectionT(std::forward<Args>(args)...), serial_(pid) {}
  const qrpc_serial_t &serial() const override { return serial_; }
protected:
  base::Serial serial_;
};
} // namespace qrpc
