#include "base/syscall.h"
#include "base/address.h"

namespace base {
  int Syscall::GetSockAddrFromFd(Fd fd, Address &a) {
    sockaddr_storage sa;
    socklen_t salen = sizeof(sa);
    if (getsockname(fd, reinterpret_cast<sockaddr *>(&sa), &salen) != 0) {
      logger::error({{"ev","Failed to get sockaddr from fd"},
        {"fd",fd},{"errno",Errno()}});
      return QRPC_ESYSCALL;
    }
    a = Address(sa, salen);
    return QRPC_OK;
  }
  Fd Syscall::Connect(
    const Address &a, bool in6,
    int send_buffer_size,
    int recv_buffer_size
  ) {
    return Connect(a.sa(), a.salen(), in6, send_buffer_size, recv_buffer_size);
  }
  std::vector<Address> &Syscall::GetIfAddrs() {
    thread_local static std::vector<Address> addrs;
    if (addrs.size() == 0) {
      struct ifaddrs *ifaddrs;
      if (getifaddrs(&ifaddrs) != 0) {
        logger::die({{"ev","getifaddrs() fails"},{"errno",Errno()}});
        return addrs;
      }
      for (auto p = ifaddrs; p != nullptr; p = p->ifa_next) {
        if (p->ifa_addr == nullptr) { continue; }
        if (p->ifa_addr->sa_family != AF_INET && p->ifa_addr->sa_family != AF_INET6) {
          continue; // skip non-ipv4/ipv6 addresses
        }
        if (p->ifa_flags & IFF_LOOPBACK) { continue; }
        if ((p->ifa_flags & (IFF_UP|IFF_RUNNING)) != (IFF_UP|IFF_RUNNING)) { continue; }
        auto a = Address(*p->ifa_addr);
        if (!a.inet_family()) { continue; }
        logger::info({{"ev","found interface"},{"ifname",p->ifa_name},{"address",a.hostip()}});
        addrs.push_back(a);
      }
    }
    return addrs;
  }  
  Fd Syscall::Accept(Fd listener_fd, Address &a, bool in6) {
    struct sockaddr_storage sa;
    socklen_t salen = sizeof(sa);
    Fd afd = Accept(listener_fd, sa, salen, in6);
    if (afd < 0) {
      return INVALID_FD;
    }
    a.Reset(sa, salen);
    return afd;
  }

}