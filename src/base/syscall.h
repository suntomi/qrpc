#pragma once

#include <unistd.h>
#include <errno.h>
#ifdef OS_LINUX
#include <linux/net_tstamp.h>
#endif
#include <arpa/inet.h>
#include <fcntl.h>
#include <string.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <net/if.h>

#include "base/address.h"
#include "base/defs.h"
#include "base/endian.h"

namespace base {

#if defined(__ENABLE_EPOLL__) || defined(__ENABLE_KQUEUE__)
typedef int Fd;
constexpr Fd INVALID_FD = -1; 
#elif defined(__ENABLE_IOCP__)
//TODO(iyatomi): windows definition?
#else
typedef int Fd;
constexpr Fd INVALID_FD = -1; 
#endif

#ifndef SO_RXQ_OVFL
#define SO_RXQ_OVFL 40
#endif

class Syscall {
public:
  // buffer size for cmsghdr. much simpler than old chromium's kCmsgSpaceForReadPacket
  static inline constexpr size_t kDefaultUdpPacketControlBufferSize = 512;

  // The maximum packet size of any QUIC packet over IPv6, based on ethernet's max
  // size, minus the IP and UDP headers. IPv6 has a 40 byte header, UDP adds an
  // additional 8 bytes.  This is a total overhead of 48 bytes.  Ethernet's
  // max packet size is 1500 bytes,  1500 - 48 = 1452.
  static inline constexpr size_t kMaxV6PacketSize = 1452;
  // The maximum packet size of any QUIC packet over IPv4.
  // 1500(Ethernet) - 20(IPv4 header) - 8(UDP header) = 1472.
  static inline constexpr size_t kMaxV4PacketSize = 1472;
  // The maximum incoming packet size allowed.
  static inline constexpr size_t kMaxIncomingPacketSize = kMaxV4PacketSize;
  // The maximum outgoing packet size allowed.
  static inline constexpr size_t kMaxOutgoingPacketSize = kMaxV6PacketSize;

  static inline int Close(Fd fd) { return ::close(fd); }
  static inline int Errno() { return errno; }
  static inline std::string StrError(int err = -1) {
    thread_local static char err_buff[256];
    auto sz = strerror_r(err < 0 ? Errno() : err, err_buff, sizeof(err_buff));
    return std::string(err_buff, sz);
  }
  static inline bool EAgain() {
    int eno = Errno();
    return (EINTR == eno || EAGAIN == eno || EWOULDBLOCK == eno);
  }
  /* note that we treat EADDRNOTAVAIL and ENETUNREACH as blocked error, when reachability_tracked. 
  because it typically happens during network link change (eg. wifi-cellular handover)
  and make QUIC connections to close. but soon link change finished and 
  QUIC actually can continue connection after that. add above errnos will increase
  possibility to migrate QUIC connection on another socket.
  
  TODO(iyatomi): more errno, say, ENETDOWN should be treated as blocked error? 
  basically we add errno to WriteBlocked list with evidence based policy. 
  that is, you actually need to see the new errno on write error caused by link change, 
  to add it to this list.
  */
  static inline bool WriteMayBlocked(int eno, bool reachability_tracked) {
    if (eno == EAGAIN || eno == EWOULDBLOCK) {
      return true;
    } else if (reachability_tracked && (eno == EADDRNOTAVAIL || eno == ENETUNREACH)) {
      return true;
    } else {
      return false;
    }
  }
  static int SetListenerAddress(
    struct sockaddr_storage &addr, uint16_t port, bool in6
  ) {
    if (in6) {
      auto tmp = (struct sockaddr_in6 *)&addr;
      tmp->sin6_len = GetSockAddrLen(AF_INET6);
      tmp->sin6_family = AF_INET6;
      tmp->sin6_addr = IN6ADDR_ANY_INIT;
      tmp->sin6_port = Endian::HostToNet(port);
      return tmp->sin6_len;
    } else {
      auto tmp = (struct sockaddr_in *)&addr;
      tmp->sin_len = GetSockAddrLen(AF_INET);
      tmp->sin_family = AF_INET;
      tmp->sin_addr.s_addr = htonl(INADDR_ANY);
      tmp->sin_port = Endian::HostToNet(port);
      return tmp->sin_len;
    }
    return true;
  }
  static const void *GetSockAddrPtr(const struct sockaddr_storage &addr) {
    if (addr.ss_family == AF_INET) {
      auto tmp = (struct sockaddr_in *)&addr;
      return &tmp->sin_addr;
    } else if (addr.ss_family == AF_INET6) {
      auto tmp = (struct sockaddr_in6 *)&addr;
      return &tmp->sin6_addr;
    } else {
      TRACE("invalid ss_family:%u", addr.ss_family);
      ASSERT(false);
      return nullptr;
    }
  }
  static int GetSockAddrPort(const struct sockaddr_storage &addr) {
    if (addr.ss_family == AF_INET) {
      auto tmp = (struct sockaddr_in *)&addr;
      return Endian::NetToHost(tmp->sin_port);
    } else if (addr.ss_family == AF_INET6) {
      auto tmp = (struct sockaddr_in6 *)&addr;
      return Endian::NetToHost(tmp->sin6_port);
    } else {
      TRACE("invalid ss_family:%u", addr.ss_family);
      ASSERT(false);
      return -1;
    }
  }
  static socklen_t GetSockAddrLen(int address_family) {
    switch(address_family) {
    case AF_INET:
      return sizeof(struct sockaddr_in);
      break;
    case AF_INET6:
      return sizeof(struct sockaddr_in6);
      break;
    default:
      logger::fatal({
        {"ev", "unsupported address family"},
        {"address_family", address_family}
      });
      ASSERT(false);
      return 0;
    }
  }
  static socklen_t GetIpAddrLen(int address_family) {
    switch(address_family) {
    case AF_INET:
      return sizeof(struct in_addr);
      break;
    case AF_INET6:
      return sizeof(struct in6_addr);
      break;
    default:
      logger::fatal({
        {"ev", "unsupported address family"},
        {"address_family", address_family}
      });
      ASSERT(false);
      return 0;
    }
  }
  static bool SetNonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
      int saved_errno = errno;
      char buf[256];
      logger::fatal({
        {"ev", "fcntl() to get flags fails"},
        {"fd", fd},
        {"errno", saved_errno},
        {"strerror", strerror_r(saved_errno, buf, sizeof(buf))}
      });
      return false;
    }
    if (!(flags & O_NONBLOCK)) {
      int saved_flags = flags;
      flags = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
      if (flags == -1) {
        int saved_errno = errno;
        char buf[256];
        // bad.
        logger::fatal({
          {"ev", "fcntl() to set flags fails"},
          {"fd", fd},
          {"prev_flags", saved_flags},
          {"errno", saved_errno},
          {"strerror", strerror_r(saved_errno, buf, sizeof(buf))}
        });
        return false;
      }
    }
    return true;
  }
  static const size_t kDefaultSocketReceiveBuffer = 1024 * 1024;
  static const size_t kDefaultSocketSendBuffer = 1024 * 1024;
  static int EnableReceivingSelfIp(Fd fd, int address_family) {
#if defined(OS_MACOSX)
    if (address_family == AF_INET6) {
      //for osx, IP_PKTINFO for ipv6 did not work. (at least at Sierra)
      return 0;
    }
#endif
    int get_local_ip = 1;
    int rc = setsockopt(fd, IPPROTO_IP, IP_PKTINFO, &get_local_ip,
                        sizeof(get_local_ip));
#if defined(IPV6_RECVPKTINFO)
    if (rc == 0 && address_family == AF_INET6) {
      rc = setsockopt(fd, IPPROTO_IPV6, IPV6_RECVPKTINFO, &get_local_ip,
                      sizeof(get_local_ip));
    }
#endif
    return rc;
  }

  static int EnableRecevingECN(Fd fd, int address_family, bool ipv6_only = false) {
    unsigned int set = 1;
    switch (address_family) {
      case AF_INET:
        if (setsockopt(fd, IPPROTO_IP, IP_RECVTOS, &set, sizeof(set)) != 0) {
        logger::error({{"ev","Failed to request to receive ECN on ipv4 socket"},
          {"fd",fd},{"errno",Errno()}});
          return QRPC_ESYSCALL;
        }
        break;
      case AF_INET6:
        if (setsockopt(fd, IPPROTO_IPV6, IPV6_RECVTCLASS, &set, sizeof(set)) != 0) {
          logger::error({{"ev","Failed to request to receive ECN on ipv6 socket"},
            {"fd",fd},{"errno",Errno()}});
          return QRPC_ESYSCALL;
        }
        if (!ipv6_only &&
            setsockopt(fd, IPPROTO_IP, IP_RECVTOS, &set, sizeof(set)) != 0) {
          logger::error({{"ev","Failed to request to receive ECN on ipv46 socket"},
            {"fd",fd},{"errno",Errno()}});
          return QRPC_ESYSCALL;
        }
        break;
    }
    return QRPC_OK;
  }

  static int EnableReceivingTimestamp(int fd) {
#if defined(SO_TIMESTAMPING) && defined(SOF_TIMESTAMPING_RX_SOFTWARE)
    int timestamping = SOF_TIMESTAMPING_RX_SOFTWARE | SOF_TIMESTAMPING_SOFTWARE;
    return setsockopt(fd, SOL_SOCKET, SO_TIMESTAMPING, &timestamping,
                      sizeof(timestamping));
#else
    return -1;
#endif
  }

  static bool SetSendBufferSize(int fd, size_t size) {
    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &size, sizeof(size)) != 0) {
      logger::error({{"ev", "Failed to set socket send size"},{"size", size},{"errno", Errno()}});
      return false;
    }
    return true;
  }

  static bool SetSocketReuseAddr(int fd) {
    int yes = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes)) != 0) {
        logger::error({{"ev", "setsockopt(SO_REUSEADDR) failed"},{"errno", Errno()}});
        return false;
    }
    return true;
  }

  static Fd Accept(Fd listener_fd, struct sockaddr_storage &sa, socklen_t &salen, bool in6 = false) {
    return accept(listener_fd, reinterpret_cast<struct sockaddr *>(&sa), &salen);
  }
  static Fd Accept(Fd listener_fd, Address &a, bool in6 = false) {
    struct sockaddr_storage sa;
    socklen_t salen = sizeof(sa);
    Fd afd = Accept(listener_fd, sa, salen, in6);
    if (afd < 0) {
      return INVALID_FD;
    }
    a.Reset(sa, salen);
    return afd;
  }

  static int Bind(Fd fd, int port, bool in6 = false) {
    struct sockaddr_storage sas;
    socklen_t salen = sizeof(sas);
    if ((salen = SetListenerAddress(sas, port, in6)) < 0) {
      logger::error({{"ev", "fail to create listner address"},{"errno", Errno()},
        {"port", port},{"in6", in6}});
      return QRPC_EINVAL;
    }
    if (bind(fd, reinterpret_cast<struct sockaddr *>(&sas), salen) < 0) {
      logger::error({{"ev", "bind() fails"},{"errno", Errno()}});
      return QRPC_ESYSCALL;
    }
    return QRPC_OK;
  }

  static Fd Connect(const sockaddr *sa, socklen_t salen, bool in6 = false) {
    int fd = socket(in6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
      logger::error({{"ev", "socket() failed"},{"errno", Errno()}});
      return INVALID_FD;
    }
    
    if (!SetNonblocking(fd)) {
      Close(fd);
      return INVALID_FD;
    }

    // nonblocking connect may returns EAGAIN
    if (connect(fd, sa, salen) < 0 && !EAgain()) {
      Close(fd);
      return INVALID_FD;
    }

    return fd;
  }
  static Fd Connect(const Address &a, bool in6 = false) {
    return Connect(a.sa(), a.salen(), in6);
  }

  static Fd Listen(int port, bool in6 = false) {
    constexpr int MAX_BACKLOG = 128;
    int fd = socket(in6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
      logger::error({{"ev", "socket() failed"},{"errno", Errno()}});
      return INVALID_FD;
    }
    
    if (!SetNonblocking(fd)) {
      Close(fd);
      return INVALID_FD;
    }

    int get_overflow = 1;
    int rc = setsockopt(fd, SOL_SOCKET, SO_RXQ_OVFL, &get_overflow,
                        sizeof(get_overflow));
    if (rc < 0) {
      logger::warn({{"ev", "Socket overflow detection not supported"}});
    }

    if (!SetReceiveBufferSize(fd, kDefaultSocketReceiveBuffer)) {
      Close(fd);
      return INVALID_FD;
    }

    if (!SetSendBufferSize(fd, kDefaultSocketReceiveBuffer)) {
      Close(fd);
      return INVALID_FD;
    }

    if (!SetSocketReuseAddr(fd)) {
      Close(fd);
      return INVALID_FD;
    }

    if (Bind(fd, port, in6) != QRPC_OK) {
      Close(fd);
      return INVALID_FD;
    }

    if (listen(fd, MAX_BACKLOG) < 0) {
      logger::error({{"ev", "listen() fails"},{"errno", Errno()}});
      Close(fd);
      return INVALID_FD;
    }

    return fd;
  }

  static bool SetReceiveBufferSize(int fd, size_t size) {
    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)) != 0) {
      logger::error({{"ev", "Failed to set socket recv size"},{"size", size},{"errno", Errno()}});
      return false;
    }
    return true;
  }
  static int CreateUDPSocket(
    int address_family, bool* overflow_supported,
    int send_buffer_size = kDefaultSocketSendBuffer,
    int recv_buffer_size = kDefaultSocketReceiveBuffer
  ) {
    int fd = socket(address_family, SOCK_DGRAM, 0);
    if (fd < 0) {
      logger::error({{"ev", "socket() failed"},{"errno", Errno()}});
      return INVALID_FD;
    }
    
    if (!SetNonblocking(fd)) {
      Close(fd);
      return INVALID_FD;
    }

    int get_overflow = 1;
    int rc = setsockopt(fd, SOL_SOCKET, SO_RXQ_OVFL, &get_overflow,
                        sizeof(get_overflow));
    if (rc < 0) {
      logger::warn({{"ev", "Socket overflow detection not supported"}});
    } else {
      *overflow_supported = true;
    }

    if (!SetReceiveBufferSize(fd, recv_buffer_size)) {
      Close(fd);
      return INVALID_FD;
    }

    if (!SetSendBufferSize(fd, send_buffer_size)) {
      Close(fd);
      return INVALID_FD;
    }

    rc = EnableReceivingSelfIp(fd, address_family);
    if (rc < 0) {
      logger::error({{"ev", "IP detection not supported"},{"errno", Errno()}});
      Close(fd);
      return INVALID_FD;
    }

    rc = EnableRecevingECN(fd, address_family);
    if (rc < 0) {
      logger::info({{"ev", "congestion notification not supported"},{"errno", Errno()}});
      Close(fd);
      return INVALID_FD;
    }

    rc = EnableReceivingTimestamp(fd);
    if (rc < 0) {
      logger::warn({{"ev", "SO_TIMESTAMPING not supported"},{"errno", Errno()}});
    }

    return fd;
  }
  static void *MemCopy(void *dst, const void *src, qrpc_size_t sz) {
    return memcpy(dst, src, sz);
  }
  static void *Memdup(const void *p, qrpc_size_t sz) {
    void *r = malloc(sz);
    memcpy(r, p, sz);
    return r;
  }
  static void MemFree(void *p) {
    free(p);
  }
  static void *MemAlloc(qrpc_size_t sz) {
    return malloc(sz);
  }
  static void MemZero(void *p, qrpc_size_t sz) {
    memset(p, 0, sz);
  }
  static int Read(Fd fd, void *p, qrpc_size_t sz) {
    return read(fd, p, sz);
  }
#if defined(__QRPC_USE_RECVMMSG__)
  static inline int RecvFrom(int fd, struct mmsghdr *msgvec, unsigned int vlen, int flags = 0) {
    return recvmmsg(fd, msgvec, vlen, flags, nullptr);
  }
  static inline int SendTo(int fd, struct mmsghdr *msg, unsigned int vlen, int flags = 0) {
    return sendmsg(fd, msg, flags);
  }
#else
  static inline int SendTo(int fd, struct msghdr *msg, int flags = 0) {
    return sendmsg(fd, msg, flags);
  }
  static inline int RecvFrom(int fd, struct msghdr *msg, int flags = 0) {
    return recvmsg(fd, msg, flags);
  }
#endif
  static int Write(Fd fd, const void *p, qrpc_size_t sz) {
    return write(fd, p, sz);
  }
  static int Writev(Fd fd, const char *pp[], qrpc_size_t *psz, qrpc_size_t sz) {
    struct iovec iov[sz];
    for (size_t i = 0; i < sz; i++) {
      iov[i].iov_base = const_cast<char *>(pp[i]);
      iov[i].iov_len = psz[i];
    }
    return writev(fd, iov, sz);
  }
  static std::unique_ptr<char> ReadFile(const std::string &path, qrpc_size_t *p_size) {
    STATIC_ASSERT(sizeof(long) == sizeof(qrpc_size_t));
    std::unique_ptr<char> ptr = std::unique_ptr<char>();
    long *sz; FILE *fp;
    
    fp = fopen(path.c_str(), "r");
    if (fp == nullptr || fseek(fp, 0, SEEK_END) == -1) { goto fail; }

    sz = reinterpret_cast<long *>(p_size);
    if ((*sz = ftell(fp)) == -1) { goto fail; }
    rewind(fp);

    ptr = std::unique_ptr<char>(new char[*sz]);
    if (ptr == nullptr || fread(ptr.get(), *sz, 1, fp) <= 0) { goto fail; } 
  fail:
    if (fp != nullptr) { fclose(fp); }
    return ptr;
  }
  static int WriteFile(const std::string &path, const void *p, qrpc_size_t size) {
    FILE *fp;
    int result = -1;
    fp = fopen(path.c_str(), "wb");
    if (fp == nullptr || (result = fwrite(p, size, 1, fp)) <= 0) { goto fail; }
  fail:
    if (fp != nullptr) { fclose(fp); }
    return result;
  }
  static std::vector<Address> &GetIfAddrs() {
    thread_local static std::vector<Address> addrs;
    if (addrs.size() == 0) {
      struct ifaddrs *ifaddrs;
      if (getifaddrs(&ifaddrs) != 0) {
        logger::die({{"ev","getifaddrs() fails"},{"errno",Errno()}});
        return addrs;
      }
      for (auto p = ifaddrs; p != nullptr; p = p->ifa_next) {
        if (p->ifa_addr == nullptr) { continue; }
        if (p->ifa_flags & IFF_LOOPBACK) { continue; }
        if (!(p->ifa_flags & (IFF_UP|IFF_RUNNING))) { continue; }
        auto a = Address(*p->ifa_addr);
        if (!a.inet_family()) { continue; }
        logger::info({{"ev","found interface"},{"ifname",p->ifa_name},{"address",a.hostip()}});
        addrs.push_back(a);
      }
    }
    return addrs;
  }
};
}
