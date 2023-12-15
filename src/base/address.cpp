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
}