#pragma once

#include "base/conn.h"
#include "base/serial.h"
#include "qrpc/handle.h"

namespace qrpc {
class Connection {
public:
  Connection(base::Serial::PartitionId pid) { HandleBss::FromObject(this)->Reset(pid); }
  virtual ~Connection() = default;
public:
  const qrpc_serial_t &serial() const { return HandleBss::FromObject(this)->serial; }
  base::Serial::PartitionId partition_id() const { return base::Serial::GetPartitionId(serial()); }
  qrpc_conn_t ToHandle() const { return { .s = serial(), .p = const_cast<Connection *>(this) }; }
  static base::Connection *FromHandle(qrpc_conn_t conn) {
    auto p = reinterpret_cast<const Connection *>(conn.p);
    if (p == nullptr || !base::Serial::IsSame(HandleBss::FromObject(p)->serial, conn.s)) {
      return nullptr;
    }
    return dynamic_cast<base::Connection *>(const_cast<Connection *>(p));
  }
};
} // namespace qrpc
