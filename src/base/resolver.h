#pragma once

#include <map>

#include <ares.h>

#include "base/defs.h"
#include "base/io_processor.h"

namespace base {
class Loop;
class AsyncResolver {
 public:
  struct Config : ares_options {
    int optmask;
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

    //methods may fail sometimes
    bool SetServerHostPort(const std::string &host, int port = 53);
  };
  struct Query {
    AsyncResolver *resolver_;
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
    ~IoRequest() override {}
    // implements IoProcessor
    void OnEvent(Fd fd, const Event &e) override;
    void OnClose(Fd fd) override {}
    int OnOpen(Fd fd) override { return QRPC_OK; }

    uint32_t current_flags() const { return current_flags_; }
    bool alive() const { return alive_; }
    void set_current_flags(uint32_t f) { current_flags_ = f; }
    void set_alive(bool a) { alive_ = a; }
    Fd fd() const { return fd_; }
  };
  Channel channel_;
  std::map<Fd, IoRequest*> io_requests_;
  std::vector<Query*> queries_;
 public:
  AsyncResolver() : channel_(nullptr), io_requests_() {}
  bool Initialize(const Config &config);
  void Resolve(Query *q) { q->resolver_ = this; queries_.push_back(q); }
  void Resolve(const char *host, int family, Callback cb, void *arg);
  void Poll(Loop *l);
  inline bool Initialized() const { return channel_ != nullptr; }
  static inline int PtoN(const std::string &host, int *af, void *buff, qrpc_size_t buflen) {
    *af = host.find(':') == std::string::npos ?  AF_INET : AF_INET6;
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
      logger::error({{"msg","unsupported af"},{"af", sa->sa_family}});
      ASSERT(false);
      return -1;
    }
    if (converted != nullptr) {
      return 0;
    } else {
      logger::error({{"msg","ntop() fails"},{"errno",Syscall::Errno()}});
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
