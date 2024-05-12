#include "base/defs.h"
#include "base/session.h"
#include "base/address.h"
#include "base/resolver.h"

namespace base {
  typedef SessionFactory::FactoryMethod FactoryMethod;
  typedef SessionFactory::Session::CloseReason CloseReason;

  struct DnsQuery : public AsyncResolver::Query {
    int port_;
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
    typedef SessionFactory::DnsErrorHandler ErrorHandler;
    SessionFactory &factory_;
    FactoryMethod factory_method_;
    ErrorHandler error_handler_;
    SessionDnsQuery(SessionFactory &f, FactoryMethod m, ErrorHandler eh) :
      factory_(f), factory_method_(m), error_handler_(eh) {}
    SessionDnsQuery(SessionFactory &f, FactoryMethod m) :
      factory_(f), factory_method_(m), error_handler_(MakeDefault()) {}
    ErrorHandler MakeDefault() {
      return [this](int status) {
        Address a;
        if (a.Set("0.0.0.0", port_) < 0) {
          logger::die({{"ev", "fail to set dummy address"}, {"port", port_}});
        }
        Session *s = factory_method_(INVALID_FD, a);
        s->SetCloseReason(CreateAresCloseReason(status));
        qrpc_time_t retry_timeout = s->OnShutdown();
        if (retry_timeout > 0) {
          s->close_reason().alarm_id = factory_.alarm_processor().Set([
            &f = factory_, host = host_, port = port_, fm = factory_method_, af = family_
          ]() {
            f.Connect(host, port, fm, af);
            return 0;
          }, retry_timeout + qrpc_time_now());
        } else {
          delete s;
        }
      };
    }
    void OnComplete(int status, int timeouts, struct hostent *entries) override {
      int r;
      if (ARES_SUCCESS == status) {
        Address server_address;
        if ((r = ConvertToSocketAddress(entries, port_, server_address)) >= 0) {
          factory_.Open(server_address, factory_method_);
          return;
        } else {
          logger::error({{"ev","invalid resolved address"},{"rc",r}});
          status = ARES_ENOTFOUND;
        }
      }
      error_handler_(status);
    }
  };

  bool SessionFactory::Connect(const std::string &host, int port, FactoryMethod m, DnsErrorHandler eh, int family_pref) {
    auto q = new SessionDnsQuery(*this, m, eh);
    q->host_ = host;
    q->family_ = family_pref;
    q->port_ = port;
    loop().ares().Resolve(q);
    return true;
  }
  bool SessionFactory::Connect(const std::string &host, int port, FactoryMethod m, int family_pref) {
    auto q = new SessionDnsQuery(*this, m);
    q->host_ = host;
    q->family_ = family_pref;
    q->port_ = port;
    loop().ares().Resolve(q);
    return true;
  }

  void UdpSessionFactory::SetupPacket() {
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

  void UdpSessionFactory::ProcessPackets(int size) {
    int r;
    auto now = qrpc_time_now();
    for (int i = 0; i < size; i++) {
      auto &h = read_packets_[i].msg_hdr;
      auto a = Address(h.msg_name, h.msg_namelen);
      auto exists = sessions_.find(a);
      // this also acts as anchor that prevents deletion of session pointer
      // in Session::Close call
      Session *s;
      if (exists == sessions_.end()) {
        // use same fd of Listener
        s = Create(fd_, a, factory_method_);
        ASSERT(s != nullptr);
        logger::info({{"ev", "accept"},{"proto","udp"},{"fd",fd_},{"addr",a.str()}});
        if ((r = s->OnConnect()) < 0) {
          s->Close(QRPC_CLOSE_REASON_LOCAL, r);
          continue;
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
      } else {
        dynamic_cast<UdpSession*>(s)->Touch(now);
      }
    }
  #if defined(__QRPC_USE_RECVMMSG__)
    mmsghdr mmsg[write_buffers_.Allocated()];
    size_t count = 0;
    for (auto kv : sessions_) {
      auto s = dynamic_cast<UdpSession *>(kv.second);
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
      // TODO: handle error correctly and try to send again
      ASSERT(false);
      logger::die({{"ev", "Syscall::SendTo fails"}, {"errno", Syscall::Errno()}});
    }
    for (auto kv : sessions_) {
      auto s = dynamic_cast<UdpSession *>(kv.second);
      s->Reset();
    }
  #endif
  }

  int UdpSessionFactory::Read() {
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
          return QRPC_EAGAIN;
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
          return QRPC_EAGAIN;
        }
        logger::error({{"ev", "Syscall::RecvFrom fails"}, {"errno", eno}});
        return QRPC_ESYSCALL;
      }
      read_packets_.data()->msg_len = r;
      return 1;
    #endif
    }
}