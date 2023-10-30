#pragma once

#include <sys/socket.h>
#include <string>

namespace base {
  class Address : std::string {
  public:
    Address(const sockaddr_storage &sa, socklen_t salen) : std::string(
      reinterpret_cast<const char *>(&sa), salen
    ) {}
    Address(const Address &a) : std::string(a) {}
    Address() : std::string() {}
    const sockaddr *sa() const { return reinterpret_cast<const sockaddr *>(c_str()); }
    socklen_t salen() const { return size(); }
    int family() const { return sa()->sa_family; }
    void Reset(const sockaddr *a, socklen_t al) {
      assign(reinterpret_cast<const char *>(a), al);
    }
    void Reset(const sockaddr_storage &sa, socklen_t al) {
      Reset(reinterpret_cast<const sockaddr *>(&sa), al);
    }
  };
}