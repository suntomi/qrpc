#include "qrpc/server.h"
#include "qrpc/transport.h"

namespace qrpc {
  int Server::StartWorkers() {
    int r;
    // reserve ids with atomic operation (fetch_add), so even if multiple thread calls StartWorkers() concurrently, 
    // they can still get unique and consecutive partition ids without lock, and only need lock to update start_partition_id_.
    start_partition_id_ = Loop::ReservePartitionIds(n_worker_);
    for (uint32_t i = 0; i < n_worker_; i++) {
      if ((r = StartWorker(start_partition_id_ + i)) < 0) {
        return r;
      }
    }
    return QRPC_OK;
  }
} 
