#include "address.h"
#include "resolver.h"

namespace base {
    struct DnsQuery : public AsyncResolver::Query {
        int ConvertToSocketAddress(struct hostent *entries, int port, sockaddr_in &address) {
            if (entries == nullptr ||
                entries->h_addrtype != AF_INET ||
                entries->h_length != sizeof(in_addr_t)) {
                return QRPC_ERESOLVE;
            }
            memset(&address, 0, sizeof(address));
            address.sin_family = AF_INET;
            address.sin_port = htons(port);
            memcpy(&address.sin_addr, entries->h_addr_list[0], sizeof(in_addr_t));

            return true;
        }
        static nq_close_reason_t CreateAresCloseReason(int status) {
            auto ares_error = ares_strerror(status);
            return {
            .app = false,
            .code = NqLibraryError,
            .msg = ares_error,
            .msglen = (nq_size_t)strlen(ares_error)
            };
        }
    };

    struct SessionDnsQuery : public DnsQuery {
        SocketFactory *factory_;
        int port_;
        SessionDnsQuery() : DnsQuery(), port_(0) {}
        void OnComplete(int status, int timeouts, struct hostent *entries) override {
            if (ARES_SUCCESS == status) {
                Address server_address;
                if (ConvertToSocketAddress(entries, port_, server_address)) {
                    factory_->Create(host_, server_id, server_address, config_);
                    return;
                } else {
                    status = ARES_ENOTFOUND;
                }
            }
            nq_close_reason_t detail = CreateAresCloseReason(status);
            //call on close with empty nq_conn_t. 
            nq_conn_t empty = {{{0}}, nullptr};
            nq_closure_call(config_.client().on_close, empty, NQ_ERESOLVE, &detail, false);
        }
    };

    struct NqDnsQueryForClosure : public NqDnsQuery {
    nq_on_resolve_host_t cb_;
    void OnComplete(int status, int timeouts, struct hostent *entries) override {
        if (ARES_SUCCESS == status) {
        nq_closure_call(cb_, NQ_OK, nullptr, 
            entries->h_addr_list[0], Syscall::GetIpAddrLen(entries->h_addrtype));
        } else {
        nq_close_reason_t detail = CreateAresCloseReason(status);
        nq_closure_call(cb_, NQ_ERESOLVE, &detail, nullptr, 0);
        }
    }  
    };

    bool NqClientLoop::Resolve(int family_pref, const std::string &host, int port, const nq_clconf_t *conf) {
    auto q = new NqDnsQueryForClient(*conf);
    q->host_ = host;
    q->loop_ = this;
    q->family_ = family_pref;
    q->port_ = port;
    async_resolver_.StartResolve(q);  
    return true;
    }
    bool NqClientLoop::Resolve(int family_pref, const std::string &host, nq_on_resolve_host_t cb) {
    auto q = new NqDnsQueryForClosure;
    q->host_ = host;
    q->family_ = family_pref;
    q->cb_ = cb;
    async_resolver_.StartResolve(q);  
    return true;
    }
}