#include "qrpc/loop.h"
#include "qrpc/worker.h"

#include <mutex>

namespace qrpc {
  static base::atomic<base::Serial::PartitionId> g_next_partition_id{1};

  base::Serial::PartitionId Loop::ReservePartitionIds(uint32_t n) {
    auto start = g_next_partition_id.fetch_add(n, std::memory_order_relaxed);
    if (start == 0 || start + n < start) {
      base::logger::die({{"ev","partition id overflow"},{"start",start},{"count",n}});
    }
    return start;
  }
  int Loop::Open(int max_nfd, uint64_t timeout_ns) {
    ASSERT(partition_id() != 0);
    ASSERT(Worker::g_partition_id() == partition_id());
    return base::Loop::Open(max_nfd, timeout_ns);
  }
  Loop &Loop::OpenOrDie(int max_nfd, uint64_t timeout_ns) {
    int r;
    if ((r = Open(max_nfd, timeout_ns)) < 0) {
      base::logger::die({{"ev","Loop::Open() fails"}, {"err", r}});
    }
    return *this;
  }
  void Loop::EnsurePartitionId(PartitionId id) {
    if (id == 0) {
      // if partition_id is not specified, use global partition id for current thread.
      auto gpid = g_partition_id();
      if (gpid == 0) {
        // if global partition id for current thread is not set, reserve one and set it.
        gpid = ReservePartitionIds(1);
        g_partition_id_ = gpid;
      }
      partition_id_ = gpid;
    } else {
      if (g_partition_id() != 0 && g_partition_id() != id) {
        logger::die({
          {"ev","Loop::EnsurePartitionId(): partition id conflict with global partition id for current thread"},
          {"global_partition_id",g_partition_id()},
          {"partition_id",id}
        });
      }
      partition_id_ = id;
      g_partition_id_ = id;
    }
  }
} // namespace qrpc
