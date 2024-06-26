#pragma once

#include <map>

#include <netdb.h>

#include <ares.h>

#include "base/defs.h"
#include "base/alarm.h"
#include "base/io_processor.h"
#include "base/loop.h"

namespace base {
class Resolver {
public:
  struct Query {
    Resolver *resolver_;
    std::string host_;
    int family_;

    Query() : host_(), family_(AF_UNSPEC) {}
    virtual ~Query() {}

    virtual void OnComplete(int status, int timeouts, struct hostent *hostent) = 0;  
    static void OnComplete(void *arg, int status, int timeouts, struct hostent *hostent) {
      auto q = (Query *)arg;
      if (q->family_ == AF_INET && (status == ARES_ENOTFOUND || status == ARES_ENODATA)) {
        q->family_ = AF_INET6;
        q->resolver_->Resolve(q);
        return;
      }
      q->OnComplete(status, timeouts, hostent);
      delete q;
    }
    int ConvertToSocketAddress(struct hostent *entries, int port, Address &addr) {
      if (entries == nullptr) {
          return QRPC_ERESOLVE;
      }
      struct sockaddr_storage address;
      memset(&address, 0, sizeof(address));
      switch (entries->h_addrtype) {
        case AF_INET: {
          auto *sa = reinterpret_cast<struct sockaddr_in *>(&address);
          sa->sin_family = entries->h_addrtype;
          sa->sin_port = Endian::HostToNet(static_cast<in_port_t>(port));
          sa->sin_len = sizeof(sockaddr_in);
          memcpy(&sa->sin_addr, entries->h_addr_list[0], sizeof(in_addr_t));
          addr.Reset(*sa);
        } break;
        case AF_INET6: {
          auto *sa = reinterpret_cast<sockaddr_in6 *>(&address);
          sa->sin6_family = entries->h_addrtype;
          sa->sin6_port = Endian::HostToNet(static_cast<in_port_t>(port));          
          sa->sin6_len = sizeof(sockaddr_in6);
          memcpy(&sa->sin6_addr, entries->h_addr_list[0], sizeof(in6_addr_t));
          addr.Reset(*sa);
        } break;
        default:
          return QRPC_ERESOLVE;
      }
      return 0;
    }
  };
public:
  virtual ~Resolver() {}
  virtual void Resolve(Query *q) = 0;
};
class NopResolver : public Resolver {
public:
  void Resolve(Query *q) override {
    ASSERT(false); // forget to configure resolver?
    q->OnComplete(QRPC_ENOTSUPPORT, 0, nullptr);
  }
  static NopResolver &Instance() {
    static NopResolver instance;
    return instance;
  }
};
class AsyncResolver : public Resolver {
 public:
  struct Config : ares_options {
    int optmask;
    qrpc_time_t granularity;
    ares_addr_port_node *server_list;
    Config();
    ~Config();
    const ares_options *options() const { 
      return static_cast<const ares_options*>(this); }
    //no fail methods
    void SetTimeout(qrpc_time_t timeout);
    void SetRotateDns();
    void SetStayOpen();
    void SetLookup(bool use_hosts, bool use_dns);
    void SetGranularity(qrpc_time_t g) { granularity = g; }

    //methods may fail sometimes
    bool SetServerHostPort(const std::string &host, int port = 53);
  };
  typedef ares_host_callback Callback;  
  typedef ares_channel Channel;
 protected:
  typedef Fd Fd;
  typedef IoProcessor::Event Event;
  class IoRequest : public IoProcessor {
   private:
    uint32_t current_flags_;
    bool alive_;
    Channel channel_;
    Fd fd_;
   public:
    IoRequest(Channel channel, Fd fd, uint32_t flags) : 
      current_flags_(flags), alive_(true), channel_(channel), fd_(fd) {}
    virtual ~IoRequest() {}
    // implements IoProcessor
    void OnEvent(Fd fd, const Event &e) override;

    uint32_t current_flags() const { return current_flags_; }
    bool alive() const { return alive_; }
    void set_current_flags(uint32_t f) { current_flags_ = f; }
    void set_alive(bool a) { alive_ = a; }
    Fd fd() const { return fd_; }
  };
  Channel channel_;
  Loop &loop_;
  AlarmProcessor::Id alarm_id_;
  std::map<Fd, IoRequest*> io_requests_;
  std::vector<Query*> queries_;
public:
  AsyncResolver(Loop &l) : channel_(nullptr), loop_(l),
    alarm_id_(AlarmProcessor::INVALID_ID), io_requests_(), queries_() {}
  ~AsyncResolver() { Finalize(); }
public:
  // implements Resolver
  void Resolve(Query *q) override { q->resolver_ = this; queries_.push_back(q); }
public:
  bool Initialize(const Config &config = Config());
  void Finalize();
  void Resolve(const char *host, int family, Callback cb, void *arg);
  void Poll();
  inline bool Initialized() const { return channel_ != nullptr; }
  static inline int PtoN(const std::string &host, int *af, void *buff, qrpc_size_t buflen) {
    *af = (host.find(':') == std::string::npos ? AF_INET : AF_INET6);
    if (Syscall::GetIpAddrLen(*af) > buflen) {
      ASSERT(false);
      return -1;
    }
    if (ares_inet_pton(*af, host.c_str(), buff) >= 0) {
      return Syscall::GetIpAddrLen(*af);
    } else {
      return -1;
    }
  }
  static inline int NtoP(const void *src, qrpc_size_t srclen, char *dst, qrpc_size_t dstlen) {
    const char *converted = nullptr;
    const sockaddr *sa = reinterpret_cast<const sockaddr *>(src);
    if (sa->sa_family == AF_INET) {
      const sockaddr_in *sin = reinterpret_cast<const sockaddr_in *>(sa);
      converted = ares_inet_ntop(AF_INET, &sin->sin_addr, dst, dstlen);
    } else if (sa->sa_family == AF_INET6) {
      const sockaddr_in6 *sin6 = reinterpret_cast<const sockaddr_in6 *>(sa);
      converted = ares_inet_ntop(AF_INET6, &sin6->sin6_addr, dst, dstlen);
    } else {
      logger::error({{"ev","unsupported af"},{"af", sa->sa_family}});
      ASSERT(false);
      return -1;
    }
    if (converted != nullptr) {
      return 0;
    } else {
      logger::error({{"ev","ntop() fails"},{"errno",Syscall::Errno()}});
      return -1;
    }
  }
  static inline std::string ParseSockAddr(const sockaddr_storage &addr) {
    char buffer[256];
    const void *sa_ptr = Syscall::GetSockAddrPtr(addr);
    if (sa_ptr == nullptr) {
      return std::string("unknown address family:") + std::to_string(addr.ss_family);
    }
    if (AsyncResolver::NtoP(sa_ptr, Syscall::GetIpAddrLen(addr.ss_family), buffer, sizeof(buffer)) < 0) {
      return std::string("unknown ip address length:") + std::to_string(Syscall::GetIpAddrLen(addr.ss_family));
    }
    return std::string(buffer) + ":" + std::to_string(Syscall::GetSockAddrPort(addr));
  }
  static inline std::string ParseSockAddr(const sockaddr &addr) {
    return ParseSockAddr(*(reinterpret_cast<const sockaddr_storage*>(&addr)));
  }
  static inline std::string ParseSockAddr(const sockaddr_in &in_addr) {
    return ParseSockAddr(*(reinterpret_cast<const sockaddr_storage*>(&in_addr)));
  }
  static inline std::string ParseSockAddr(const sockaddr_in6 &in6_addr) {
    return ParseSockAddr(*(reinterpret_cast<const sockaddr_storage*>(&in6_addr)));
  }
};
}
