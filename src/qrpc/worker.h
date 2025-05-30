#pragma once

#include <map>
#include <thread>

#include "moodycamel/concurrentqueue.h"

#include "qrpc/base.h"
#include "qrpc/listener.h"

namespace qrpc {
class Server;
class Dispatcher;
class Worker {
public:
  typedef base::Loop Loop;
  typedef std::function<void()> Task;
  typedef moodycamel::ConcurrentQueue<Task> TaskQueue;
private:
  uint32_t index_; // worker index
  Server &server_;
  Loop loop_;
  std::thread thread_; // actually runs event loop
 public:
  Worker(uint32_t index, Server &server) : 
    index_(index), server_(server), loop_(), thread_() {}
  void Run(TaskQueue &q);
  std::vector<std::unique_ptr<Listener>> Listen();
  inline void Start(TaskQueue &q) {
    thread_ = std::thread([this, &q]() { this->Run(q); });
  }
  inline void Join() {
    if (thread_.joinable()) { thread_.join(); }
  }
  int GlobalPortIndex(int port_index) const;
  HandlerMap &HandlerMapFor(int port_index);

  //accessor
  inline const Server &server() const { return server_; }
  inline Server &server() { return server_; }
  inline Loop &loop() { return loop_; }
  inline uint32_t index() const { return index_; }
  inline std::thread::id thread_id() const { return thread_.get_id(); }

 private:
  DISALLOW_COPY_AND_ASSIGN(Worker);
};
}