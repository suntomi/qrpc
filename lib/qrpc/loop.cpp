#include "qrpc/loop.h"
#include "qrpc/worker.h"

#include <mutex>

namespace qrpc {
  static base::atomic<base::Serial::PartitionId> g_next_partition_id{1};
  thread_local Loop::PartitionId Loop::g_partition_id_{0};

  Loop::PartitionId Loop::ReservePartitionIds(uint32_t n) {
    auto start = g_next_partition_id.fetch_add(n, std::memory_order_relaxed);
    if (start == 0 || start + n < start) {
      base::logger::die({{"ev","partition id overflow"},{"start",start},{"count",n}});
    }
    return start;
  }
  int Loop::Open(int max_nfd, uint64_t timeout_ns) {
    ASSERT(partition_id() != 0);
    ASSERT(g_partition_id() == partition_id());
    return base::Loop::Open(max_nfd, timeout_ns);
  }
  Loop &Loop::OpenOrDie(int max_nfd, uint64_t timeout_ns) {
    int r;
    if ((r = Open(max_nfd, timeout_ns)) < 0) {
      base::logger::die({{"ev","Loop::Open() fails"}, {"err", r}});
    }
    return *this;
  }
  void Loop::EnsurePartitionId(PartitionId gpid) {
    if (partition_id_ != 0) {
      logger::die({
        {"ev","Loop::EnsurePartitionId() called more than once for same Loop object"},
        {"partition_id",partition_id_},{"this",base::str::dptr(this)}
      });
    }
    if (g_partition_id_ == 0) {
      if (gpid == 0) {
        // if global partition id for current thread is not set and gpid not specified explicitly, 
        // reserve one and set it.
        g_partition_id_ = ReservePartitionIds(1);
      } else {
        // if global partition id for current thread is not set and gpid specified explicitly, use it.
        g_partition_id_ = gpid;
      }
      logger::info({
        {"ev","set new partition id for current thread"},
        {"gpid", g_partition_id_},{"this",base::str::dptr(this)}
      });
    } else if (g_partition_id_ != gpid) {
      logger::die({
        {"ev","Loop::EnsurePartitionId() called with different partition id for same Loop object"},
        {"current_gpid",g_partition_id_},{"new_gpid",gpid},{"this",base::str::dptr(this)}
      });
    }
    partition_id_ = g_partition_id_;
  }
} // namespace qrpc
