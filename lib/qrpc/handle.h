#pragma once

#include <cstddef>

#include "base/allocator.h"
#include "base/serial.h"

namespace qrpc {
struct alignas(std::max_align_t) HandleBss {
  qrpc_serial_t serial;

  HandleBss() {
    base::Serial::Clear(serial);
  }

  void Reset(base::Serial::PartitionId partition_id) {
    base::Serial next(partition_id);
    serial = next;
  }

  void Clear() {
    base::Serial::Clear(serial);
  }

  static HandleBss *FromObject(const void *ptr) {
    return base::BssFromObject<HandleBss>(ptr);
  }
};
}
