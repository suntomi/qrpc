#include "qrpc/worker.h"
#include "qrpc/listener.h"
#include "qrpc/server.h"

namespace qrpc {
void Worker::Run(TaskQueue &q) {
  auto ls = Listen();
  qrpc_time_t next_try_accept = 0;
  while (server_.alive()) {
    //TODO(iyatomi): better way to handle this (eg. with timer system)
    qrpc_time_t now = qrpc_time_now();
    bool try_accept = false;
    if ((next_try_accept + qrpc_time_msec(10)) < now) {
      try_accept = true;
      next_try_accept = now;
    }
    //consume queue
    Task t;
    while (q.try_dequeue(t)) { t(); }
    loop_.Poll();
  }
}
std::vector<std::unique_ptr<Listener>> Worker::Listen() {
  std::vector<std::unique_ptr<Listener>> ls;
  int n_listener = server_.port_configs().size();
  int port_index = 0;
  auto sv = server_.ToHandle();
  for (auto &kv : server_.port_configs()) {
    auto l = std::make_unique<Listener>(sv);
    if (!l->Listen(*this, port_index++, kv.second.addr, kv.second)) {
      QRPC_LOGJ(fatal, {{"ev", "Listener::Listen() failed"},{"port", kv.second.addr.port}});
      continue;
    }
    ls.push_back(std::move(l));
  }
  return ls;
}
} //namespace qrpc
