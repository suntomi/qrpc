#pragma once

#include <map>
#include <thread>

#include "moodycamel/concurrentqueue.h"

#include "base/serial.h"
#include "qrpc/base.h"
#include "qrpc/listener.h"

namespace qrpc {
class Server;
class Dispatcher;
class Worker {
public:
  typedef base::Loop Loop;
  typedef base::Serial::PartitionId PartitionId;
  typedef std::function<void()> Task;
  typedef moodycamel::ConcurrentQueue<Task> TaskQueue;
private:
  static thread_local Server *server_;
  Loop loop_;
  std::thread thread_; // actually runs event loop
 public:
  Worker(Server &server) : 
    loop_(), thread_() { SetThreadLocal(server); }
  void Run(int max_nfd);
  std::vector<std::unique_ptr<Listener>> Listen();
  inline void Start(int max_nfd) {
    thread_ = std::thread([this, max_nfd]() { this->Run(max_nfd); });
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
  static TaskQueue &queue(PartitionId id);
 private:
  DISALLOW_COPY_AND_ASSIGN(Worker);
};
}