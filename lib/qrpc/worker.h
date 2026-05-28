#pragma once

#include <map>
#include <thread>

#include "moodycamel/concurrentqueue.h"

#include "base/serial.h"
#include "qrpc/base.h"
#include "qrpc/listener.h"
#include "qrpc/loop.h"

namespace qrpc {
class Server;
class Dispatcher;
class Worker {
public:
  typedef qrpc::Loop Loop;
  typedef base::Serial::PartitionId PartitionId;
  typedef std::function<void()> Task;
  typedef moodycamel::ConcurrentQueue<Task> TaskQueue;
private:
  static thread_local Server *server_;
  Loop loop_;
  std::thread thread_; // actually runs event loop
public:
  Worker() : loop_(), thread_() {}
  void Run(PartitionId partition_id, Server &sv);
  std::vector<std::unique_ptr<Listener>> Listen();
  inline void Start(PartitionId partition_id, Server &server) {
    thread_ = std::thread([this, partition_id, &server]() { this->Run(partition_id, server); });
  }
  inline void Join() {
    if (thread_.joinable()) { thread_.join(); }
  }
  int GlobalPortIndex(int port_index) const;
  void SetThreadLocal(Server &server);

  //accessor
  inline const Server &server() const { return *server_; }
  inline Server &server() { return *server_; }
  inline Loop &loop() { return loop_; }
  inline std::thread::id thread_id() const { return thread_.get_id(); }
  static inline PartitionId g_partition_id() { return qrpc::Loop::g_partition_id(); }
  static inline bool HasServer() { return server_ != nullptr; }
  static TaskQueue &queue(PartitionId id);
private:
  DISALLOW_COPY_AND_ASSIGN(Worker);
};
}
