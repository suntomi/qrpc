namespace base {
  struct ClosureDnsQuery : public DnsQuery {
    qrpc_on_resolve_host_t cb_;
    void OnComplete(int status, int timeouts, struct hostent *entries) override {
      if (ARES_SUCCESS == status) {
        qrpc_closure_call(cb_, QRPC_OK, nullptr, 
          entries->h_addr_list[0], Syscall::GetIpAddrLen(entries->h_addrtype));
      } else {
        qrpc_closure_call(cb_, QRPC_ERESOLVE, nullptr, 
          "", 0);
      }
    }  
  };
  bool SessionFactory::Resolve(int family_pref, const std::string &host, qrpc_on_resolve_host_t cb) {
    auto q = new NqDnsQueryForClosure;
    q->host_ = host;
    q->family_ = family_pref;
    q->cb_ = cb;
    async_resolver_.StartResolve(q);  
    return true;
  }
}