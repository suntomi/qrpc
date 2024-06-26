#include "core/nq_worker.h"

#include "base/syscall.h"
#include "core/nq_client_loop.h"
#include "core/nq_dispatcher.h"
#include "core/nq_server_session.h"
#include "core/nq_server.h"

namespace qrpc {
void Worker::Process(Packet *p) {
  // usually size of dispatcher isn't large, simple for-loop should run faster
  for (size_t i = 0; i < dispatchers_.size(); i++) {
    if (dispatchers_[i].first == p->port()) {
      dispatchers_[i].second->Process(p);
      return;
    }
  }
  ASSERT(false);
}
void Worker::Run(PacketQueue &pq) {
  int n_dispatcher = server_.port_configs().size();
  InvokeQueue *iq[n_dispatcher];
  Dispatcher *ds[n_dispatcher];
  if (!Listen(iq, ds)) {
    logger::fatal("fail to listen");
    exit(1);
    return;
  }
  Packet *p;
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
    while (pq.try_dequeue(p)) {
      //pass packet to corresponding session
      Process(p);
    }
    //wait and process incoming event
    for (int i = 0; i < n_dispatcher; i++) {
      iq[i]->Poll(ds[i]);
      if (try_accept) {
        ds[i]->Accept();
      }
    }
    loop_.Poll();
  }
  //shutdown proc
  bool per_worker_shutdown_state[n_dispatcher];
  for (int i = 0; i < n_dispatcher; i++) {
    per_worker_shutdown_state[i] = false;
    ds[i]->Shutdown(); //send connection close for all sessions handled by this worker
  }
  //last consume queue with checking all sessions are gone
  bool need_wait_shutdown = true;
  auto shutdown_start = qrpc_time_now();
  while (need_wait_shutdown) {
    while (pq.try_dequeue(p)) {
      //pass packet to corresponding session
      Process(p);
    }
    //wait and process incoming event
    need_wait_shutdown = false;
    for (int i = 0; i < n_dispatcher; i++) {
      iq[i]->Poll(ds[i]);
      if (per_worker_shutdown_state[i]) {
      } else if (!ds[i]->ShutdownFinished(shutdown_start)) {
        need_wait_shutdown = true;
      } else {
        per_worker_shutdown_state[i] = true;
      }
    }
    loop_.Poll();
  }
}
bool Worker::Listen(InvokeQueue **iq, Dispatcher **ds) {
  if (loop_.Open(server_.port_configs().size()) < 0) {
    ASSERT(false);
    return false;
  }
  int port_index = 0;
  for (auto &kv : server_.port_configs()) {
    QuicSocketAddress address;
    if (!ToSocketAddress(kv.second.address_, address)) {
      ASSERT(false);
      return false;
    }
    auto listen_fd = CreateUDPSocketAndBind(address);
    if (listen_fd < 0) {
      ASSERT(false);
      return false;
    }
    logger::info({
      {"ev", "listen"},
      {"thread_index", index_}, 
      {"fd", listen_fd},
    });
    auto d = new Dispatcher(kv.first, kv.second, *this);
    d->SetFromConfig(kv.second);
    if (loop_.Add(listen_fd, d, Loop::EV_READ | Loop::EV_WRITE) != QRPC_OK) {
      Syscall::Close(listen_fd);
      delete d;
      ASSERT(false);
      return false;
    }
    ds[port_index] = d;
    iq[port_index] = d->invoke_queues() + index_;
    port_index++;
    dispatchers_.push_back(std::pair<int, Dispatcher*>(kv.first, d));
  }
  return true;
}
//helper
Fd Worker::CreateUDPSocketAndBind(const QuicSocketAddress& address) {
  Fd fd = Syscall::CreateUDPSocket(address.family(), &overflow_supported_);
  if (fd < 0) {
    logger::error({
      {"ev", "CreateSocket() failed"},
      {"errno", errno}, 
      {"strerror", strerror(errno)}
    });
    return -1;
  }

  //set socket resuable
  int flag = 1, rc = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &flag, sizeof(flag));
  if (rc < 0) {
    logger::error({
      {"ev", "setsockopt(SO_REUSEPORT) failed"},
      {"errno", errno}, 
      {"strerror", strerror(errno)}
    });
    Syscall::Close(fd);
    return -1;    
  }

  sockaddr_storage addr = address.generic_address();
  socklen_t slen = Syscall::GetSockAddrLen(addr.ss_family);
  if (slen == 0) {
    Syscall::Close(fd);
    return -1;
  }
  rc = bind(fd, reinterpret_cast<sockaddr*>(&addr), slen);
  if (rc < 0) {
    logger::error({
      {"ev", "Bind failed"},
      {"errno", errno}, 
      {"strerror", strerror(errno)}
    });
    Syscall::Close(fd);
    return -1;
  }
  logger::error({
    {"ev", "Listening start"},
    {"address", address.ToString()}
  });
  return fd; 
}
/* static */
bool Worker::ToSocketAddress(const qrpc_addr_t &addr, QuicSocketAddress &socket_address) {
  char buffer[sizeof(struct sockaddr_storage)];
  int len, af;
  if ((len = AsyncResolver::PtoN(addr.host, &af, &buffer, sizeof(buffer))) < 0) {
    return false;
  }
  return QuicSocketAddress::FromPackedString(buffer, len, addr.port, socket_address);
}
} //namespace nq
