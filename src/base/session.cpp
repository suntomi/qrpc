#include "base/defs.h"
#include "base/session.h"
#include "base/address.h"
#include "base/resolver.h"

namespace base {
  typedef SessionFactory::FactoryMethod FactoryMethod;
  typedef SessionFactory::Session::CloseReason CloseReason;

  std::string CertificatePair::TryAutogen(const std::vector<std::string> &ifaddrs) {
    if (!need_autogen()) { return ""; }
    std::pair<std::string, std::string> cp;
    auto r = cert::gen(cp, ifaddrs);
    if (!r.empty()) {
      return r;
    }
    QRPC_LOGJ(info, {{"ev","autogen certpair"},{"cert",cp.first},{"pkey",cp.second}});
    auto certpath = Syscall::MkTemp("/tmp/qrpc_auto_cert");
    auto privkeypath = Syscall::MkTemp("/tmp/qrpc_auto_pkey");
    if (Syscall::WriteFile(certpath, cp.first.c_str(), cp.first.size()) < 0) {
      return "Failed to write certificate";
    }
    if (Syscall::WriteFile(privkeypath, cp.second.c_str(), cp.second.size()) < 0) {
      return "Failed to write private key";
    }
    cert = certpath;
    privkey = privkeypath;
    return "";
  }

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

  SessionFactory::SessionFactory(SessionFactory &&rhs) :
    factory_method_(std::move(rhs.factory_method_)),
    loop_(rhs.loop_),
    resolver_(rhs.resolver_),
    alarm_processor_(rhs.alarm_processor_),
    alarm_id_(AlarmProcessor::INVALID_ID),
    cert_(rhs.cert_), privkey_(rhs.privkey_),
    session_timeout_(rhs.session_timeout_),
    is_listener_(rhs.is_listener_) {
    if (rhs.alarm_id_ != AlarmProcessor::INVALID_ID) {
      rhs.loop_.alarm_processor().Cancel(rhs.alarm_id_);
      rhs.alarm_id_ = AlarmProcessor::INVALID_ID;
    }
    if (rhs.tls_ctx_ != nullptr) {
      tls_ctx_ = rhs.tls_ctx_;
      rhs.tls_ctx_ = nullptr;
    }
    Init();
  }

  void SessionFactory::Init() {
      if (session_timeout() > 0) {
          alarm_id_ = alarm_processor_.Set(
              [this]() { return this->CheckTimeout(); }, qrpc_time_now() + session_timeout()
          );
      }
      if (!need_tls()) { return; }
      char err_buf[256];
      tls_ctx_ = SSL_CTX_new(TLS_method());
      if (tls_ctx_ == nullptr) {
          ERR_error_string_n(ERR_get_error(), err_buf, sizeof(err_buf));
          logger::die({{"ev", "Failed to create SSL context"}, {"err", err_buf}});
      }
      // サーバー証明書のロード
      if (SSL_CTX_use_certificate_file(tls_ctx_, cert_.c_str(), SSL_FILETYPE_PEM) <= 0) {
          ERR_error_string_n(ERR_get_error(), err_buf, sizeof(err_buf));
          logger::die({{"ev", "Failed to load certificate"}, {"err", err_buf}});
      }

      // 秘密鍵のロード
      if (SSL_CTX_use_PrivateKey_file(tls_ctx_, privkey_.c_str(), SSL_FILETYPE_PEM) <= 0) {
          ERR_error_string_n(ERR_get_error(), err_buf, sizeof(err_buf));
          logger::die({{"ev", "Failed to load private key"}, {"err", err_buf}});
      }

      // 秘密鍵と証明書の整合性確認
      if (SSL_CTX_check_private_key(tls_ctx_) != 1) {
          ERR_error_string_n(ERR_get_error(), err_buf, sizeof(err_buf));
          logger::die({{"ev", "Private key does not match the certificate"}, {"err", err_buf}});
      }						
  }
  void SessionFactory::Fin() {
      if (alarm_id_ != AlarmProcessor::INVALID_ID) {
          alarm_processor_.Cancel(alarm_id_);
          alarm_id_ = AlarmProcessor::INVALID_ID;
      }
      if (tls_ctx_ != nullptr) {
          SSL_CTX_free(tls_ctx_);
          tls_ctx_ = nullptr;
      }
  }  

  bool SessionFactory::Connect(const std::string &host, int port, FactoryMethod m, DnsErrorHandler eh, int family_pref) {
    auto q = new SessionDnsQuery(*this, m, eh);
    q->host_ = host;
    q->family_ = family_pref;
    q->port_ = port;
    resolver_.Resolve(q);
    return true;
  }
  bool SessionFactory::Connect(const std::string &host, int port, FactoryMethod m, int family_pref) {
    auto q = new SessionDnsQuery(*this, m);
    q->host_ = host;
    q->family_ = family_pref;
    q->port_ = port;
    resolver_.Resolve(q);
    return true;
  }

  int UdpSessionFactory::UdpSession::Flush() {
    auto size = write_vecs_.size();
  #if defined(__QRPC_USE_RECVMMSG__)
    mmsghdr mmsg[size];
    for (size_t idx = 0; idx < size; idx++) {
      auto &iov = write_vecs()[idx];
      auto &h = mmsg[idx].msg_hdr;
      h.msg_name = const_cast<sockaddr *>(addr().sa());
      h.msg_namelen = addr().salen();
      h.msg_iov = &iov;
      h.msg_iovlen = 1;
      h.msg_control = nullptr;
      h.msg_controllen = 0;
      mmsg[idx].msg_len = 0;
    }
    int r;
    if ((r = Syscall::SendTo(fd_, mmsg, size)) < 0) {
      if (Syscall::IOMayBlocked(r, false)) {
        return count; // nothing should be sent
      }
      ASSERT(false);
      QRPC_LOGJ(error, {{"ev", "Syscall::SendTo fails"}, {"errno", Syscall::Errno()}});
    }
    ASSERT(r <= size);
    Reset(r);
    return size - r;
  #else
    for (size_t idx = 0; idx < size; idx++) {
      auto &iov = write_vecs_[idx];
      if (iov.iov_len <= 0) {
          continue;
      }
      // TODO: batched sendto for sendmmsg environment
      struct msghdr h;
      h.msg_name = const_cast<sockaddr *>(addr().sa());
      h.msg_namelen = addr().salen();
      h.msg_iov = &iov;
      h.msg_iovlen = 1;
      h.msg_control = nullptr;
      h.msg_controllen = 0;
      if (Syscall::SendTo(fd_, &h) < 0) {
        // reset with sent count (idx)
        Reset(idx);
        if (Syscall::IOMayBlocked(Syscall::Errno(), false)) {
            return size - idx;
        }
        QRPC_LOGJ(error, {{"ev","SendTo fails"},{"fd",fd_},{"errno",Syscall::Errno()}});
        ASSERT(false);
        return QRPC_ESYSCALL;
      }
    }
    Reset(size);
  #endif
    return QRPC_OK;
  }

  int UdpListener::Flush() {
    size_t n_writebuf = write_buffers_.Allocated();
  #if defined(__QRPC_USE_RECVMMSG__)
    mmsghdr mmsg[n_writebuf];
    auto n_sessions = sessions_.size();
    int sends[n_sessions];
    UdpSession *sessions[n_sessions];
    size_t count = 0, session_idx = 0;
    for (auto kv : sessions_) {
      auto s = dynamic_cast<UdpSession *>(kv.second);
      auto size = s->write_vecs().size();
      sends[session_idx] = size;
      sessions[session_idx] = s;
      session_idx++;
      for (size_t idx = 0; idx < size; idx++) {
        auto &iov = s->write_vecs()[idx];
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
    int r;
    if ((r = Syscall::SendTo(fd_, mmsg, count)) < 0) {
      if (Syscall::IOMayBlocked(r, false)) {
        return count; // nothing should be sent
      }
      ASSERT(false);
      QRPC_LOGJ(error, {{"ev", "Syscall::SendTo fails"}, {"errno", Syscall::Errno()}});
      return count;
    }
    if (r < count) {
      auto remain = count - r;
      // partially sent. reset iovs due to sent count
      for (size_t idx; idx < session_idx; idx++) {
        auto s = sessions[idx];
        if (sends[idx] < r) {
          s->Reset(sends[idx]);
          r -= sends[idx];
        } else {
          s->Reset(sends[idx] - r);
          break;
        }
      }
      return remain;
    } else {
      // all sent
      for (size_t idx; idx < session_idx; idx++) {
        auto s = sessions[idx];
        s->Reset(sends[idx]);
      }
      return 0;
    }
  #else
    for (auto kv : sessions_) {
      auto s = dynamic_cast<UdpSession *>(kv.second);
      int r = s->Flush();
      if (r != 0) {
        return n_writebuf - write_buffers_.Allocated();
      }
    }
    return 0;
  #endif
  }

  UdpListener::UdpListener(UdpListener &&rhs) :
    UdpSessionFactory(std::move(rhs)),
    fd_(rhs.fd_),
    port_(rhs.port_),
    overflow_supported_(rhs.overflow_supported_),
    sessions_(std::move(rhs.sessions_)),
    read_packets_(batch_size_),
    read_buffers_(batch_size_) {
    rhs.fd_ = INVALID_FD;
    SetupPacket();
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
    // send all buffered packets and start flush task if unsent packets remain
    TryFlush();
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
      if (Syscall::IOMayBlocked(eno, false)) {
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
      if (Syscall::IOMayBlocked(eno, false)) {
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