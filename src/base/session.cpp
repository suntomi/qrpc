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

  void UdpListener::SetupPacket() {
    for (int i = 0; i < batch_size_; i++) {
      auto &h = read_packets_[i].msg_hdr;
      h.msg_name = &read_buffers_[i].sa;
      h.msg_namelen = sizeof(read_buffers_[i].sa);
      h.msg_iov = &read_buffers_[i].iov;
      h.msg_iov->iov_base = read_buffers_[i].buf;
      h.msg_iov->iov_len = Syscall::kMaxIncomingPacketSize;
      h.msg_iovlen = 1;
      h.msg_control = read_buffers_[i].cbuf;
      h.msg_controllen = Syscall::kDefaultUdpPacketControlBufferSize;
      read_packets_[i].msg_len = 0;
    }
  }

  void UdpListener::ProcessPackets(int size) {
    int r;
    for (int i = 0; i < size; i++) {
      auto &h = read_packets_[i].msg_hdr;
      auto a = Address(h.msg_name, h.msg_namelen);
      auto exists = sessions_.find(a);
      Session *s;
      if (exists == sessions_.end()) {
        // use same fd of Listener
        s = Create(fd_, a, factory_method_);
        ASSERT(s != nullptr);
        logger::info({{"ev", "accept"},{"proto","udp"},{"fd",fd_},{"addr",a.str()}});
        if ((r = s->OnConnect()) < 0) {
          s->Close(QRPC_CLOSE_REASON_LOCAL, r);
          delete s;
          continue;
        }
        if (timeout() > 0) {
          ASSERT(alarm_processor_ != nullptr);
          auto h = [s]() { return reinterpret_cast<UdpSession *>(s)->CheckTimeout(); };
          if (alarm_processor_->Set(h, qrpc_time_now() + timeout()) < 0) {
            s->Close(QRPC_CLOSE_REASON_LOCAL, QRPC_EALLOC, "fail to register alarm");
            delete s;
            continue;
          }
        }
      } else {
        s = exists->second;
      }
      ASSERT(s != nullptr);
      if ((r = s->OnRead(
        reinterpret_cast<const char *>(h.msg_iov->iov_base),
        read_packets_[i].msg_len
      )) < 0) {
        s->Close(QRPC_CLOSE_REASON_LOCAL, r);
        delete s;
      }
    }
  #if defined(__QRPC_USE_RECVMMSG__)
    mmsghdr mmsg[write_buffers_.Allocated()];
    size_t count = 0;
    for (auto kv : sessions_) {
      auto s = reinterpret_cast<UdpSession *>(kv.second);
      for (auto &iov : s->write_vecs()) {
        auto &h = mmsg[count++].msg_hdr;
        h.msg_name = const_cast<sockaddr *>(s->addr().sa());
        h.msg_namelen = s->addr().salen();
        h.msg_iov = &iov;
        h.msg_iovlen = 1;
        h.msg_control = nullptr;
        h.msg_controllen = 0;
        mmsg[count - 1].msg_len = 0;
      }
    }
    if (Syscall::SendTo(fd_, mmsg, count) < 0) {
      logger::die({{"ev", "Syscall::SendTo fails"}, {"errno", Syscall::Errno()}});
    }
    for (auto kv : sessions_) {
      auto s = reinterpret_cast<UdpSession *>(kv.second);
      s->Reset();
    }
  #endif
  }

  int UdpListener::Read() {
    for (int i = 0; i < batch_size_; i++) {
      auto &h = read_packets_[i].msg_hdr;
      h.msg_namelen = sizeof(read_buffers_[i].sa);
      h.msg_iov->iov_len = Syscall::kMaxIncomingPacketSize;
      h.msg_controllen = Syscall::kDefaultUdpPacketControlBufferSize;
      read_packets_[i].msg_len = 0;
    }
    #if defined(__QRPC_USE_RECVMMSG__)
      int r = Syscall::RecvFrom(fd_, read_packets_.data(), batch_size_);
      if (r < 0) {
        int eno = Syscall::Errno();
        if (Syscall::WriteMayBlocked(eno, false)) {
          return QRPC_OK;
        }
        logger::error({{"ev", "Syscall::RecvFrom fails"}, {"errno", eno});
        return QRPC_ESYSCALL;
      }
      return r;
    #else
      int r = Syscall::RecvFrom(fd_, &read_packets_.data()->msg_hdr);
      if (r < 0) {
        int eno = Syscall::Errno();
        if (Syscall::WriteMayBlocked(eno, false)) {
          return QRPC_OK;
        }
        logger::error({{"ev", "Syscall::RecvFrom fails"}, {"errno", eno}});
        return QRPC_ESYSCALL;
      }
      read_packets_.data()->msg_len = r;
      return 1;
    #endif
    }
}