#include "base/defs.h"
#include "base/session.h"
#include "base/address.h"
#include "base/resolver.h"
#include <netdb.h>

namespace base {
  typedef SessionFactory::FactoryMethod FactoryMethod;
  typedef SessionFactory::Session::CloseReason CloseReason;

  struct DnsQuery : public AsyncResolver::Query {
    int port_;
    int ConvertToSocketAddress(struct hostent *entries, int port, Address &addr) {
      if (entries == nullptr) {
          return QRPC_ERESOLVE;
      }
      struct sockaddr_storage address;
      memset(&address, 0, sizeof(address));
      switch (entries->h_addrtype) {
        case AF_INET: {
          auto *sa = reinterpret_cast<struct sockaddr_in *>(&address);
          sa->sin_family = entries->h_addrtype;
          sa->sin_port = htons(port);
          memcpy(&sa->sin_addr, entries->h_addr_list[0], sizeof(in_addr_t));
          addr.Reset(reinterpret_cast<struct sockaddr *>(sa), Syscall::GetIpAddrLen(AF_INET));
        } break;
        case AF_INET6: {
          auto *sa = reinterpret_cast<sockaddr_in6 *>(&address);
          sa->sin6_family = entries->h_addrtype;
          sa->sin6_port = htons(port);          
          memcpy(&sa->sin6_addr, entries->h_addr_list[0], sizeof(in6_addr_t));
          addr.Reset(reinterpret_cast<struct sockaddr *>(sa), Syscall::GetIpAddrLen(AF_INET6));
        } break;
        default:
          return QRPC_ERESOLVE;
      }
      return 0;
    }
    static CloseReason CreateAresCloseReason(int status) {
      auto ares_error = ares_strerror(status);
      return {
        .code = QRPC_CLOSE_REASON_RESOLVE,
        .detail_code = status,
        .msg = ares_error,
      };
    }
  };

  struct SessionDnsQuery : public DnsQuery {
    SessionFactory *factory_;
    FactoryMethod factory_method_;
    void OnComplete(int status, int timeouts, struct hostent *entries) override {
      if (ARES_SUCCESS == status) {
        Address server_address;
        if (ConvertToSocketAddress(entries, port_, server_address)) {
          factory_->Open(server_address, factory_method_);
          return;
        } else {
          status = ARES_ENOTFOUND;
        }
      }
      CloseReason detail = CreateAresCloseReason(status);
      Session *s = factory_method_(INVALID_FD, Address());
      s->Close(detail);
      // s is freed inside Close call.
    }
  };

  bool SessionFactory::Resolve(int family_pref, const std::string &host, int port, FactoryMethod m) {
    auto q = new SessionDnsQuery;
    q->factory_ = this;
    q->factory_method_ = m;
    q->host_ = host;
    q->family_ = family_pref;
    q->port_ = port;
    loop().ares().Resolve(q);
    return true;
  }
}