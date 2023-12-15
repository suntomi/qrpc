#pragma once

#include <sys/socket.h>
#include <string>

namespace base {
  class Address : public std::string {
  public:
    Address(const sockaddr_storage &sa, socklen_t salen) : std::string(
      reinterpret_cast<const char *>(&sa), salen
    ) {}
    Address(const sockaddr &sa) : std::string(
      reinterpret_cast<const char *>(&sa), sa.sa_len
    ) {}
    Address(const void *p, socklen_t salen) : std::string(
      reinterpret_cast<const char *>(p), salen
    ) {}
    Address(const Address &a) : std::string(a) {}
    Address() : std::string() {}
    const sockaddr *sa() const { return reinterpret_cast<const sockaddr *>(c_str()); }
    socklen_t salen() const { return size(); }
    int family() const { return sa()->sa_family; }
    bool inet_family() const { return family() == AF_INET || family() == AF_INET6; }
    std::string hostip() const;
    int port() const;
    std::string str() const { return hostip() + ":" + std::to_string(port()); }
    void Reset(const sockaddr *a, socklen_t al) {
      assign(reinterpret_cast<const char *>(a), al);
    }
    void Reset(const sockaddr_storage &sa, socklen_t al) {
      Reset(reinterpret_cast<const sockaddr *>(&sa), al);
    }
  };
}