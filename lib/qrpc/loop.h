#pragma once

#include "base/loop.h"
#include "base/serial.h"
#include "base/atomic.h"
#include "base/logger.h"

namespace qrpc {
class Loop : public base::Loop {
  typedef base::Serial::PartitionId PartitionId;
  static thread_local PartitionId g_partition_id_;
  PartitionId partition_id_{0};
public:
  using PartitionId = base::Serial::PartitionId;
  explicit Loop(PartitionId partition_id = 0) : base::Loop() { EnsurePartitionId(partition_id); }
  inline PartitionId partition_id() const { return partition_id_; }
  static inline PartitionId g_partition_id() { return g_partition_id_; }
  static PartitionId ReservePartitionIds(uint32_t n);
  void EnsurePartitionId(PartitionId id);
  int Open(int max_nfd, uint64_t timeout_ns = 1000 * 1000);
  Loop &OpenOrDie(int max_nfd, uint64_t timeout_ns = 1000 * 1000);
  inline void Poll() {
    ASSERT(g_partition_id() == partition_id());
    base::Loop::Poll();
  }
  inline void WaitEvent() {
    ASSERT(g_partition_id() == partition_id());
    base::Loop::WaitEvent();
  }
};
} // namespace qrpc
