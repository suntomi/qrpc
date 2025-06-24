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
  int Address::Set(const std::string &host, uint16_t port) {
    int af, r;
    char buff[256], abuff[256];
    if ((r = AsyncResolver::PtoN(host, &af, abuff, sizeof(abuff))) >= 0) {
      r = Syscall::GetSockAddrLen(af);
      if (af == AF_INET) {
        auto *sin = reinterpret_cast<sockaddr_in *>(buff);
        sin->sin_family = af; // no endian conversion of 1byte value
        sin->sin_port = Endian::HostToNet(port);
        sin->sin_addr = *reinterpret_cast<struct in_addr *>(abuff);
        #if !OS_LINUX
        sin->sin_len = r; // no endian conversion of 1byte value
        #endif
        assign(buff, r);
        return 0;
      } else if (af == AF_INET6) {
        auto *sin6 = reinterpret_cast<sockaddr_in6 *>(buff);
        sin6->sin6_family = af; // no endian conversion of 1byte value
        sin6->sin6_port = Endian::HostToNet(port);
        sin6->sin6_addr = *reinterpret_cast<struct in6_addr *>(abuff);
        #if !OS_LINUX
        sin6->sin6_len = r; // no endian conversion of 1byte value
        #endif
        sin6->sin6_flowinfo = 0;
        sin6->sin6_scope_id = 0;
        assign(buff, r);
        return 0;
      } else {
        // af does not supported
        ASSERT(false);
        return QRPC_ENOTSUPPORT;
      }
    } else {
      return r;
    }
  }
}