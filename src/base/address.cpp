#include "base/address.h"
#include "base/resolver.h"

namespace base {
  std::string Address::hostip() const {
    char buff[128];
    AsyncResolver::NtoP(c_str(), length(), buff, sizeof(buff));
    return std::string(buff);
  }
  int Address::port() const {
    return Syscall::GetSockAddrPort(*reinterpret_cast<const sockaddr_storage *>(sa()));
  }
  int Address::Set(const std::string &host, int port) {
    int af, r;
    char buff[256];
    if ((r = AsyncResolver::PtoN(host, &af, buff, sizeof(buff))) >= 0) {
      auto *sa = reinterpret_cast<sockaddr *>(const_cast<char *>(buff));
      if (af == AF_INET) {
        auto *sin = reinterpret_cast<sockaddr_in *>(sa);
        sin->sin_family = af;
        sin->sin_port = Endian::HostToNet(port);
        assign(buff, r);
        return 0;
      } else if (af == AF_INET6) {
        auto *sin6 = reinterpret_cast<sockaddr_in6 *>(sa);
        sin6->sin6_family = af;
        sin6->sin6_port = Endian::HostToNet(port);
        assign(buff, r);
        return 0;
      } else {
        return -1;
      }
    } else {
      return -1;
    }
  }
}