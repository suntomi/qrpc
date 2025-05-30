#pragma once

#include <map>
#include <thread>

#include "base/allocator.h"
#include "core/nq_worker.h"
#include "core/nq_alarm.h"
#include "core/nq_boxer.h"
#include "core/nq_server_session.h"
#include "core/nq_static_section.h"
#include "core/nq_stream.h"
#include "core/serial.h"

namespace qrpc {
class Worker;
class ServerConfig;
class DispatcherBase : class Boxer {
 protected:
  static const int kNumSessionsToCreatePerSocketEvent = 1024;
  typedef Worker::InvokeQueue InvokeQueue;
  typedef IdFactory<NqAlarmIndex> AlarmIndexGenerator
  typedef IdFactory<NqSessionIndex> SessionIndexGenerator
  typedef Allocator<NqServerSession, StaticSection> SessionAllocator;
  typedef Allocator<NqServerStream, StaticSection> StreamAllocator;
  typedef Alarm::Allocator AlarmAllocator;
  
  int port_, accept_per_loop_; 
  uint32_t index_, n_worker_, session_limit_;
  Server &server_;
  const ServerConfig &config_;
  InvokeQueue *invoke_queues_; //only owns index_ th index. 
  ServerLoop &loop_;
  std::thread::id thread_id_;
  AlarmIndexGenerator alarm_idxgen_;
  SessionIndexGenerator session_idxgen_;
  SessionAllocator session_allocator_;
  StreamAllocator stream_allocator_;
  AlarmAllocator alarm_allocator_;

 public:
  DispatcherBase(int port, const ServerConfig& config, Worker &worker);
  virtual ~NqDispatcherBase() {}

  //interface DispatcherBase
  virtual void Shutdown() = 0;
  virtual bool HasShutdownFinished() const = 0;
  virtual void Accept() = 0;
  virtual void SetFromConfig(const ServerConfig &conf) = 0;

  //operation
  bool ShutdownFinished(qrpc_time_t shutdown_start) const;

  //get/set
  inline Loop *loop() { return &loop_; }
  inline Syscall::Random &random() { return loop().random(); }
  inline const ServerConfig &config() const { return config_; }
  inline InvokeQueue *invoke_queues() { return invoke_queues_; }
  inline int worker_num() const { return n_worker_; }
  inline int worker_index() const { return index_; }
  inline bool main_thread() const { return thread_id_ == std::this_thread::get_id(); }
  inline SessionIndexGenerator &session_idxgen() { return session_idxgen_; }
  inline StreamAllocator &stream_allocator() { return stream_allocator_; }
  //avoid confusing with QuicSession::session_allocator
  inline SessionAllocator &session_allocator_body() { return session_allocator_; }
  inline IdFactory<uint32_t> &stream_index_factory() { return server_.stream_index_factory(); }

  //implements Boxer
  void Enqueue(Op *op) override;
  bool MainThread() const override { return main_thread(); }
  Loop *Loop() override { return &loop_; }
  Alarm *NewAlarm() override;
  AlarmAllocator *GetAlarmAllocator() override { return &alarm_allocator_; }
  bool IsClient() const override { return false; }
  bool IsSessionLocked(SessionIndex idx) const override { return loop_.IsSessionLocked(idx); }
  void LockSession(SessionIndex idx) override { loop_.LockSession(idx); }
  void UnlockSession() override { loop_.UnlockSession(); }
  void RemoveAlarm(AlarmIndex index) override;

 protected:
  void AddAlarm(Alarm *a);
};
} // namespace nq