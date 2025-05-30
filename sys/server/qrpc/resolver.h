#include "qrpc.h"

#include "base/resolver.h"

namespace qrpc {
  struct ClosureDnsQuery : public base::AsyncResolver::Query {
    qrpc_on_resolve_host_t cb_;
    void OnComplete(int status, int timeouts, struct hostent *entries) override {
      if (ARES_SUCCESS == status) {
        qrpc_closure_call(cb_, QRPC_OK, nullptr,
          entries->h_addr_list[0], Syscall::GetIpAddrLen(entries->h_addrtype));
      } else {
        qrpc_closure_call(cb_, QRPC_ERESOLVE, nullptr, "", 0);
      }
    }
    static inline bool Resolve(
      base::AsyncResolver &ares, int family_pref, const std::string &host, qrpc_on_resolve_host_t cb
    ) {
      auto q = new ClosureDnsQuery;
      q->host_ = host;
      q->family_ = family_pref;
      q->cb_ = cb;
      ares.Resolve(q);  
      return true;
    }
  };
}