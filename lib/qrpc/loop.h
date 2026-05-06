#pragma once

#include "base/loop.h"
#include "base/serial.h"
#include "base/atomic.h"
#include "base/logger.h"

namespace qrpc {
class Loop : public base::Loop {
  base::Serial::PartitionId partition_id_{0};
  static thread_local base::Serial::PartitionId g_partition_id_;
public:
  inline base::Serial::PartitionId partition_id() const { return partition_id_; }
  static inline base::Serial::PartitionId g_partition_id() { return g_partition_id_; }
  inline void SetParitionId(base::Serial::PartitionId id) { partition_id_ = id; }
  void EnsurePartitionId();
  inline int Open(int max_nfd, uint64_t timeout_ns = 1000 * 1000) {
    EnsurePartitionId();
    ASSERT(partition_id() != 0);
    return base::Loop::Open(max_nfd, timeout_ns);
  }
  inline Loop &OpenOrDie(int max_nfd, uint64_t timeout_ns = 1000 * 1000) {
    int r;
    if ((r = Open(max_nfd, timeout_ns)) < 0) {
      base::logger::die({{"ev","Loop::Open() fails"}, {"err", r}});
    }
    return *this;
  }
  inline void Poll() {
    ASSERT(g_partition_id() == partition_id());
    base::Loop::Poll();
  }
};
} // namespace qrpc
