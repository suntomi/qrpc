#include "core/nq_dispatcher.h"

#include "core/nq_server_session.h"
#include "core/nq_server.h"

namespace qrpc {
DispatcherBase::NqDispatcherBase(int port, const ServerConfig& config, Worker &worker)
  : port_(port), 
  accept_per_loop_(config.server().accept_per_loop <= 0 ? kNumSessionsToCreatePerSocketEvent : config.server().accept_per_loop),
  index_(worker.index()), n_worker_(worker.server().n_worker()), 
  session_limit_(config.server().use_max_session_hint_as_limit ? config.server().max_session_hint : 0), 
  server_(worker.server()), config_(config), loop_(worker.loop()), random_(),
  thread_id_(worker.thread_id()), alarm_idxgen_(), session_idxgen_(),
  session_allocator_(config.server().max_session_hint), stream_allocator_(config.server().max_stream_hint),
  alarm_allocator_(config.server().max_session_hint) {
  invoke_queues_ = server_.InvokeQueuesFromPort(port);
  ASSERT(invoke_queues_ != nullptr);
}


//operation
bool DispatcherCompat::ShutdownFinished(qrpc_time_t shutdown_start) const { 
  if (HasShutdownFinished()) {
    logger::info({
      {"ev", "shutdown finished"},
      {"reason", "all session closed"},
      {"worker_index", index_},
      {"port", port_},
    });
    return true;
  } else if ((shutdown_start + config_.server().shutdown_timeout) < qrpc_time_now()) {
    logger::error({
      {"ev", "shutdown finished"},
      {"reason", "timeout"},
      {"worker_index", index_},
      {"port", port_},
      {"shutdown_start", shutdown_start},
      {"shutdown_timeout", config_.server().shutdown_timeout},
    });
    return true;
  } else {
    return false;
  }
}


//implements Boxer
void DispatcherBase::Enqueue(Op *op) {
  //TODO(iyatomi): DispatcherBase owns invoke_queue
  invoke_queues_[index_].enqueue(op);
}
Alarm *NqDispatcherBase::NewAlarm() {
  auto a = new(this) Alarm();
  auto idx = alarm_idxgen_.New();
  qrpc_serial_t s;
  AlarmSerialCodec::ServerEncode(s, idx);
  a->InitSerial(s);
  return a;
}
void DispatcherBase::RemoveAlarm(AlarmIndex index) {
}
}

