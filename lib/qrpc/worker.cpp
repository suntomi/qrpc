#include "qrpc/worker.h"
#include "qrpc/listener.h"
#include "qrpc/server.h"

namespace qrpc {
void Worker::Run(TaskQueue &q) {
  auto ls = Listen();
  if (ls.size() == 0) {
    QRPC_LOGJ(fatal, {{"ev" "no listener"}});
    return;
  }
  while (server_.alive()) {
    // consume queue. TODO: option to not use task queue
    Task t;
    while (q.try_dequeue(t)) { t(); }
    // TODO: option to use Poll()
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

int Worker::GlobalPortIndex(int port_index) const { 
  return (server().process_index() * server().n_worker() * server().port_configs().size()) + 
    (index() * server().port_configs().size()) +
    port_index; 
}
HandlerMap &Worker::HandlerMapFor(int port_index) {
  return server().port_configs().at(port_index).handler_map;
}

} //namespace qrpc
