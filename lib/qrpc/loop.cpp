#include "qrpc/loop.h"

namespace qrpc {
  static base::atomic<base::Serial::PartitionId> g_next_partition_id{1};
  thread_local base::Serial::PartitionId Loop::g_partition_id_ = 0;

  void Loop::EnsurePartitionId() {
    if (g_partition_id_ == 0) {
      g_partition_id_ = g_next_partition_id.fetch_add(1, std::memory_order_relaxed);
      if (g_partition_id_ == 0) {
        base::logger::die({{"ev","partition id overflow"}});
      }
    }
    partition_id_ = g_partition_id_;
  }
} // namespace qrpc
