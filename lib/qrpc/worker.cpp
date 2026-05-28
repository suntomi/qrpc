#include "qrpc/worker.h"
#include "qrpc/listener.h"
#include "qrpc/server.h"

namespace qrpc {

thread_local Server *Worker::server_ = nullptr;

void Worker::SetThreadLocal(Server &server) {
  if (server_ != nullptr) {
    logger::die({
      {"ev","Worker::SetPartitionId() called more than once"},
      {"old_server",base::str::dptr(server_)},
      {"new_server",base::str::dptr(&server)}
    });
  }
  server_ = &server;
}

Worker::TaskQueue &Worker::queue(PartitionId id) {
  ASSERT(server_ != nullptr);
  ASSERT(server_->OwnsPartitionId(id));
  return server_->queue(id);
}

void Worker::Run(PartitionId partition_id, Server &sv) {
  SetThreadLocal(sv);
  loop_.EnsurePartitionId(partition_id);
  if (loop_.Open(sv.max_nfd_per_worker()) < 0) {
    QRPC_LOGJ(fatal, {{"ev", "Loop::Open() failed in Worker::Run()"}});
    return;
  }
  auto &q = sv.AddWorker(loop_.partition_id(), this);
  auto ls = Listen();
  if (ls.size() == 0) {
    QRPC_LOGJ(fatal, {{"ev" "no listener"}});
    return;
  }
  while (sv.alive()) {
    // consume queue. TODO: option to not use task queue
    Task t;
    while (q.try_dequeue(t)) { t(); }
    // TODO: option to use Poll()
    loop_.Poll();
  }
}

std::vector<std::unique_ptr<Listener>> Worker::Listen() {
  std::vector<std::unique_ptr<Listener>> ls;
  int port_index = 0;
  for (auto &kv : server().port_configs()) {
    auto l = Listener::Create(*this, port_index++, kv.second);
    if (!l) {
      QRPC_LOGJ(fatal, {{"ev", "Listener::Listen() failed"},{"port", kv.second.ep.port}});
      continue;
    }
    ls.push_back(std::move(l));
  }
  return ls;
}

int Worker::GlobalPortIndex(int port_index) const { 
  return (server().process_index() * server().n_worker() * server().port_configs().size()) + 
    ((loop_.partition_id() - 1) * server().port_configs().size()) +
    port_index;
}

} //namespace qrpc
