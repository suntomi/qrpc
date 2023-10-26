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

#include "address.h"
#include "defs.h"
#include "endian.h"

namespace base {

#if defined(__ENABLE_EPOLL__) || defined(__ENABLE_KQUEUE__)
typedef int Fd;
constexpr Fd INVALID_FD = -1; 
#elif defined(__ENABLE_IOCP__)
//TODO(iyatomi): windows definition
#else
#endif

#ifndef SO_RXQ_OVFL
#define SO_RXQ_OVFL 40
#endif

class Syscall {
public:
  static inline int Close(Fd fd) { return ::close(fd); }
  static inline int Errno() { return errno; }
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
  static bool SetListnerAddress(
    struct sockaddr_storage &addr, uint16_t port, bool in6
  ) {
    if (in6) {
      auto tmp = (struct sockaddr_in6 *)&addr;
      tmp->sin6_len = GetSockAddrLen(AF_INET6);
      tmp->sin6_family = AF_INET6;
      tmp->sin6_addr.s_addr = htonl(INADDR_ANY);
      tmp->sin6_port = Endian::HostToNet(port);
    } else {
      auto tmp = (struct sockaddr_in *)&addr;
      tmp->sin_len = GetSockAddrLen(AF_INET);
      tmp->sin_family = AF_INET;
      tmp->sin_addr.s_addr = htonl(INADDR_ANY);
      tmp->sin_port = Endian::HostToNet(port);
    }
    return true;
  }
  static int SetSockAddr(
    struct sockaddr_storage &addr, const char *addr_p, qrpc_size_t addr_len, uint16_t port
  ) {
    if (addr_len == Syscall::GetIpAddrLen(AF_INET)) {
      auto tmp = (struct sockaddr_in *)&addr;
      tmp->sin_len = GetSockAddrLen(AF_INET);
      tmp->sin_family = AF_INET;
      tmp->sin_port = Endian::HostToNet(port);
      memcpy(&tmp->sin_addr, addr_p, addr_len);
    } else if (addr_len == Syscall::GetIpAddrLen(AF_INET6)) {
      auto tmp = (struct sockaddr_in6 *)&addr;
      tmp->sin6_len = GetSockAddrLen(AF_INET6);
      tmp->sin6_family = AF_INET6;
      tmp->sin6_port = Endian::HostToNet(port);
      memcpy(&tmp->sin6_addr, addr_p, addr_len);
    } else {
      TRACE("invalid addr_len:%u", addr_len);
      ASSERT(false);
      return -1;
    }
    return 0;
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
        {"msg", "unsupported address family"},
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
        {"msg", "unsupported address family"},
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
        {"msg", "fcntl() to get flags fails"},
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
          {"msg", "fcntl() to set flags fails"},
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
  static int SetGetAddressInfo(int fd, int address_family) {
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

  static int SetGetSoftwareReceiveTimestamp(int fd) {
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
      logger::error({
        {"msg", "Failed to set socket send size"},
        {"size", size}
      });
      return false;
    }
    return true;
  }

  static Fd Accept(Fd listener_fd, struct sockaddr_storage &sa, socklen_t &salen, bool in6 = false) {
    return accept(listener_fd, &sa, &salen);
  }
  static Fd Accept(Fd listener_fd, Address &a, bool in6 = false) {
    struct sockaddr_storage sa;
    socklen_t salen = sizeof(sa);
    Fd afd = Accept(listener_fd, sa, salen, in6);
    if (afd < 0) {
      return INVALID_FD;
    }
    a = Address(sa, salen);
    return afd;
  }

  static Fd Connect(const sockaddr_storage &sa, socklen_t salen, bool in6 = false) {
    int fd = socket(in6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
      logger::error({
        {"msg", "socket() failed"},
        {"errno", Errno()}
      });
      return INVALID_FD;
    }
    
    if (!SetNonblocking(fd)) {
      Close(fd);
      return INVALID_FD;
    }

    // nonblocking connect may returns EAGAIN
    if (connect(fd, &sa, salen) < 0 && !EAgain()) {
      Close(fd);
      return INVALID_FD;
    }

    return fd;
  }
  static Fd Connect(const Address &a, bool in6 = false) {
    return Connect(a.sa(), a.salen(), in6);
  }

  static Fd Listen(int port, bool in6 = false) {
    const int MAX_BACKLOG = 128;
    int fd = socket(in6 ? AF_INET6 : AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
      logger::error({
        {"msg", "socket() failed"},
        {"errno", Errno()}
      });
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
      logger::warn({
        {"msg", "Socket overflow detection not supported"}
      });
    } else {
      *overflow_supported = true;
    }

    if (!SetReceiveBufferSize(fd, kDefaultSocketReceiveBuffer)) {
      Close(fd);
      return INVALID_FD;
    }

    if (!SetSendBufferSize(fd, kDefaultSocketReceiveBuffer)) {
      Close(fd);
      return INVALID_FD;
    }

    struct sockaddr_storage sas;
    if (!SetSockAddr(sas, port, in6)) {
      Close(fd);
      return INVALID_FD;
    }

    if (bind(fd, (struct sockaddr *)&sas, sizeof(sas)) < 0) {
      Close(fd);
      return INVALID_FD;
    }

    if (listen(fd, MAX_BACKLOG) < 0) {
      Close(fd);
      return INVALID_FD;
    }

    return fd;
  }

  static bool SetReceiveBufferSize(int fd, size_t size) {
    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size)) != 0) {
      logger::error({
        {"msg", "Failed to set socket recv size"},
        {"size", size}
      });
      return false;
    }
    return true;
  }
  static int CreateUDPSocket(int address_family, bool* overflow_supported) {
    int fd = socket(address_family, SOCK_DGRAM, 0);
    if (fd < 0) {
      logger::error({
        {"msg", "socket() failed"},
        {"errno", Errno()}
      });
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
      logger::warn({
        {"msg", "Socket overflow detection not supported"}
      });
    } else {
      *overflow_supported = true;
    }

    if (!SetReceiveBufferSize(fd, kDefaultSocketReceiveBuffer)) {
      Close(fd);
      return INVALID_FD;
    }

    if (!SetSendBufferSize(fd, kDefaultSocketReceiveBuffer)) {
      Close(fd);
      return INVALID_FD;
    }

    rc = SetGetAddressInfo(fd, address_family);
    if (rc < 0) {
      logger::error({
        {"msg", "IP detection not supported"},
        {"errno", Errno()}
      });
      Close(fd);
      return INVALID_FD;
    }

    rc = SetGetSoftwareReceiveTimestamp(fd);
    if (rc < 0) {
      logger::warn({
        {"msg", "SO_TIMESTAMPING not supported; using fallback"},
        {"errno", Errno()}
      });
    }

    return fd;
  }
  static size_t Sprintf(char *buff, qrpc_size_t sz, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buff, sz, fmt, ap);
    va_end(ap);
    return r;
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
  static int Write(Fd fd, void *p, qrpc_size_t sz) {
    return write(fd, p, sz);
  }
  static int Writev(Fd fd, void **pp, qrpc_size_t *psz, qrpc_size_t sz) {
    struct iovec iov[sz];
    for (size_t i = 0; i < sz; i++) {
      iov[i].iov_base = pp[i];
      iov[i].iov_len = psz[i];
    }
    return writev(fd, pp, sz);
  }
  static std::unique_ptr<uint8_t> ReadFile(const std::string &path, qrpc_size_t *p_size) {
    STATIC_ASSERT(sizeof(long) == sizeof(qrpc_size_t));
    std::unique_ptr<uint8_t> ptr = std::unique_ptr<uint8_t>(nullptr);
    long *sz; FILE *fp;
    
    fp = fopen(path.c_str(), "r");
    if (fp == nullptr || fseek(fp, 0, SEEK_END) == -1) { goto fail; }

    sz = reinterpret_cast<long *>(p_size);
    if ((*sz = ftell(fp)) == -1) { goto fail; }
    rewind(fp);

    ptr = std::unique_ptr<uint8_t>(new uint8_t[*sz]);
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

};
}
