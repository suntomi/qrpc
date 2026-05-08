#pragma once

#include "base/loop.h"
#include "base/serial.h"
#include "base/atomic.h"
#include "base/logger.h"

namespace qrpc {
class Loop : public base::Loop {
public:
  typedef base::Serial::PartitionId PartitionId;
  explicit Loop() : Loop(false) {}
  explicit Loop(bool ensure_now) : base::Loop() {
    if (ensure_now) { EnsurePartitionId(); }
  }
  inline PartitionId partition_id() const { return partition_id_; }
  static inline PartitionId g_partition_id() { return g_partition_id_; }
  static PartitionId ReservePartitionIds(uint32_t n);
  // call before Open() and Poll(), WaitEvent() start to be called from owner thread.
  void EnsurePartitionId(PartitionId gpid = 0);
  // if Loop's owner thread will be changed, call this to set new partition id.
  inline void ResetPartitionId() { partition_id_ = 0; EnsurePartitionId(); }
  int Open(int max_nfd, uint64_t timeout_ns = 1000 * 1000);
  Loop &OpenOrDie(int max_nfd, uint64_t timeout_ns = 1000 * 1000);
  inline void Poll() {
    ASSERT(partition_id() != 0);
    ASSERT(g_partition_id() == partition_id());
    base::Loop::Poll();
  }
  inline void WaitEvent() {
    ASSERT(partition_id() != 0);
    ASSERT(g_partition_id() == partition_id());
    base::Loop::WaitEvent();
  }
protected:
  static thread_local PartitionId g_partition_id_;
  PartitionId partition_id_{0};
};
} // namespace qrpc
