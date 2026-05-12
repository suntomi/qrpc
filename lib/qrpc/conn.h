#pragma once

#include "base/conn.h"
#include "base/serial.h"

namespace qrpc {
class Connection {
public:
  Connection(base::Serial::PartitionId pid) : serial_(pid) {}
  virtual ~Connection() = default;
public:
  const qrpc_serial_t &serial() const { return serial_; }
  qrpc_conn_t ToHandle() const { return { .s = serial(), .p = const_cast<Connection *>(this) }; }
  static base::Connection *FromHandle(qrpc_conn_t conn) {
    auto p = reinterpret_cast<const Connection *>(conn.p);
    if (p == nullptr || !base::Serial::IsSame(p->serial(), conn.s)) {
      return nullptr;
    }
    return dynamic_cast<base::Connection *>(const_cast<Connection *>(p));
  }
protected:
  base::Serial serial_;
};
} // namespace qrpc
