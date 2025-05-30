#pragma once

#include <map>
#include <thread>

#include "moodycamel/concurrentqueue.h"

#include "core/compat/nq_worker_compat.h"
#include "core/nq_server_loop.h"
#include "core/nq_boxer.h"

namespace qrpc {
class Server;
class Dispatcher;
class Worker : class WorkerCompat {
  uint32_t index_;
  Server &server_;
  ServerLoop loop_;
  std::thread thread_;
  //TODO(iyatomi): measture this to confirm
  //almost case, should only have a few element. I think linear scan of vector faster
  std::vector<std::pair<int, Dispatcher*>> dispatchers_;
  bool overflow_supported_;
 public:
  typedef moodycamel::ConcurrentQueue<NqPacket*> PacketQueue;
  typedef Boxer::Processor InvokeQueue;
  Worker(uint32_t index, Server &server) : 
    WorkerCompat(), index_(index), server_(server), loop_(),
    thread_(), dispatchers_(), overflow_supported_(false) {}
  void Start(PacketQueue &pq) {
    thread_ = std::thread([this, &pq]() { Run(pq); });
  }
  void Process(Packet *p);
  bool Listen(InvokeQueue **iq, Dispatcher **ds);
  void Run(PacketQueue &queue);
  void Join() {
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  //accessor
  inline const Server &server() const { return server_; }
  inline ServerLoop &loop() { return loop_; }
  inline uint32_t index() { return index_; }
  inline Server &server() { return server_; }
  inline std::thread::id thread_id() const { return thread_.get_id(); }

 protected:
  static bool ToSocketAddress(const qrpc_addr_t &addr, QuicSocketAddress &address);
  Fd CreateUDPSocketAndBind(const QuicSocketAddress& address);

 private:
  DISALLOW_COPY_AND_ASSIGN(Worker);
};
}