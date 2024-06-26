#include "base/resolver.h"
#include "base/loop.h"

namespace base {
// optmask, server_list, flags, timeout, lookups are member of the class
AsyncResolver::Config::Config() : optmask(0), granularity(qrpc_time_msec(10)), server_list(nullptr) {
  flags = 0;
}
AsyncResolver::Config::~Config() {
  while (server_list != nullptr) {
    auto tmp = server_list;
    server_list = server_list->next;
    delete tmp;
  }
  server_list = nullptr;
}
void AsyncResolver::Config::SetTimeout(qrpc_time_t t_o) {
  flags |= ARES_OPT_TIMEOUTMS;
  timeout = (int)qrpc_time_to_msec(t_o);
}
bool AsyncResolver::Config::SetServerHostPort(const std::string &host, int port) {
  auto tmp = new struct ares_addr_port_node;
  int af;
  if (AsyncResolver::PtoN(host.c_str(), &af, &(tmp->addr), sizeof(tmp->addr)) > 0) {
    tmp->family = af;
    tmp->udp_port = tmp->tcp_port = port;
  } else {
    Syscall::MemFree(tmp);
    return false;
  }
  tmp->next = server_list;
  server_list = tmp;
  return true;
}
void AsyncResolver::Config::SetRotateDns() {
  optmask |= ARES_OPT_ROTATE;
}
void AsyncResolver::Config::SetStayOpen() {
  optmask |= ARES_FLAG_STAYOPEN;
}
void AsyncResolver::Config::SetLookup(bool use_hosts, bool use_dns) {
  if (use_hosts) {
    if (use_dns) {
      lookups = const_cast<char *>("bf");
    } else {
      lookups = const_cast<char *>("b");
    }
  } else {
    lookups = const_cast<char *>("f");
  }
}

void AsyncResolver::IoRequest::OnEvent(Fd fd, const Event &e) {
  if (Loop::Readable(e)) {
    if (Loop::Writable(e)) {
      ares_process_fd(channel_, fd, fd);
    } else {
      ares_process_fd(channel_, fd, ARES_SOCKET_BAD);
    }
  } else {
    ares_process_fd(channel_, ARES_SOCKET_BAD, fd);
  }
}
bool AsyncResolver::Initialize(const Config &config) {
  int status = ares_init_options(&channel_, 
    const_cast<ares_options *>(config.options()), config.optmask);
  if(status != ARES_SUCCESS) {
    logger::error({
      {"ev", "fail ares_init_options"},
      {"error", ares_strerror(status)}
    });
    return false;
  }
  ares_set_servers_ports(channel_, config.server_list);
  if (loop_.alarm_processor().Set([this, interval = config.granularity](){
      Poll();
      return qrpc_time_now() + interval;
  }, qrpc_time_now()) < 0) {
    logger::error({{"ev", "fail to set alarm"}});
    return false;
  }
  return true;
}
void AsyncResolver::Finalize() {
  if (alarm_id_ != AlarmProcessor::INVALID_ID) {
    loop_.alarm_processor().Cancel(alarm_id_);
    alarm_id_ = AlarmProcessor::INVALID_ID;
  }
}
void AsyncResolver::Resolve(const char *host, int family, ares_host_callback cb, void *arg) {
  ASSERT(channel_ != nullptr);
  ares_gethostbyname(channel_, host, family, cb, arg);
}
void AsyncResolver::Poll() {
  ASSERT(channel_ != nullptr);
  Fd fds[ARES_GETSOCK_MAXNUM];
  auto bits = ares_getsock(channel_, fds, ARES_GETSOCK_MAXNUM);
  //synchronize socket i/o request with event loop registration state
  for (auto &kv : io_requests_) {
    kv.second->set_alive(false);
  }
  if (bits != 0) {
    for (int i = 0; i < ARES_GETSOCK_MAXNUM; i++) {
      uint32_t flags = 0;
      if(ARES_GETSOCK_READABLE(bits, i)) {
        flags |= Loop::EV_READ;
      }
      if(ARES_GETSOCK_WRITABLE(bits, i)) {
        flags |= Loop::EV_WRITE;
      }
      if (flags != 0) {
        Fd fd = fds[i];
        auto it = io_requests_.find(fd);
        if (it != io_requests_.end()) {
          ASSERT(fd == it->second->fd());
          if (it->second->current_flags() != flags) {
            TRACE("ares: fd mod: %d %x => %x", fd, it->second->current_flags(), flags);
            if (loop_.Mod(fd, flags) < 0) {
              ASSERT(false);
              //will remove below
            } else {
              it->second->set_current_flags(flags);
              it->second->set_alive(true);
            }
          } else {
            //no need to change
            it->second->set_alive(true);
          }
        } else {
          TRACE("ares: fd add: %d %x", fd, flags);
          // TODO: use emplace
          auto req = new IoRequest(channel_, fd, flags);
          if (loop_.Add(fd, req, flags) < 0) {
            ASSERT(false);
            delete req;
          } else {
            io_requests_[fd] = req;
          }
        }
      }
    }
  }
  for (auto it = io_requests_.begin(); it != io_requests_.end(); ) {
    auto it_prev = it;
    it++;
    if (!it_prev->second->alive()) {
      auto req = it_prev->second;
      io_requests_.erase(it_prev);
      TRACE("ares: fd del: %d", req->fd());
      //fd already closed in ares internal and reused by another object (eg. Client)
      //also, fd seems closed in ares library when control path comes here.
      loop_.ForceDelWithCheck(req->fd(), req);
      delete req;
    }
  }
  if (queries_.size() > 0) {
    for (auto q : queries_) {
      switch (q->family_) {
        case AF_INET6:
          //AF_UNSPEC automatically search AF_INET6 => AF_INET addresses
          Resolve(q->host_.c_str(), AF_UNSPEC, Query::OnComplete, q);
          break;
        default:
          //if AF_INET and not found, then Query::OnComplete re-invoke with AF_INET6
          Resolve(q->host_.c_str(), q->family_, Query::OnComplete, q);
          break;
      }
    }
    queries_.clear();
  }
  //TODO(iyatomi): if bits == 0, pause executing this Polling
  //then activate again if any Resolve call happens.
}
}