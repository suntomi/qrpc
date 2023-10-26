#pragma once

#include <sys/socket.h>

namespace base {
  class Address : std::string {
  public:
    Address(const sockaddr_storage &sa, socklen_t salen) : std::string(
      reinterpret_cast<const char *>(&sa), salen
    ) {}
    Address(const Address &a) : std::string(a) {}
    const sockaddr *sa() const { return reinterpret_cast<const sockaddr *>(c_str()); }
    socklen_t salen() const { return size(); }
    int family() const { return sa()->sa_family; }
  };
}