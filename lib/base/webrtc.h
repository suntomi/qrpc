#pragma once

#include "base/conn.h"
#include "base/http.h"
#include "base/id_factory.h"
#include "base/session.h"
#include "base/media.h"
#include "base/webrtc/ice.h"
#include "base/rtp/handler.h"
#include "base/webrtc/candidate.h"
// this need to declare after ice.h to prevent from IceServer.hpp being used

// TODO: if enabling srtp, this also need to be replaced with homebrew version
#include "RTC/DtlsTransport.hpp"
#include "RTC/SrtpSession.hpp"
#include "RTC/RTCP/Packet.hpp"

#include <mutex>

namespace base {
namespace webrtc {
  typedef base::Stream Stream;
  typedef base::AdhocStream AdhocStream;
  typedef base::AlarmProcessor AlarmProcessor;
  typedef base::Connection BaseConnection;
  typedef IceProber::TxId TxId;

  // ConnectionFactory
  class ConnectionFactory {
  public:
    typedef std::string IceUFrag;
    typedef rtp::MediaStreamConfig::ControlOptions ControlOptions;
    struct CloseReason {
      qrpc_close_reason_code_t code;
      int64_t detail_code;
      std::string msg;
      qrpc_close_reason_t To() {
        return {
          .code = code,
          .detail_code = detail_code,
          .msg = msg.c_str(),
          .msglen = msg.length()
        };
      }
    };
    typedef std::function<Connection *(ConnectionFactory &, RTC::DtlsTransport::Role)> FactoryMethod;
    struct Port {
      enum Protocol {
        NONE = QRPC_EPPROTOCOL_NONE,
        UDP = QRPC_EPPROTOCOL_UDP,
        TCP = QRPC_EPPROTOCOL_TCP,
        ALL = QRPC_EPPROTOCOL_ALL,
      };
      Protocol protocol;
      int port;
    };
    struct Endpoint {
      std::string host{""}, path{"/qrpc"}, ip{""};
      int port{0};
      Port::Protocol protocol{Port::Protocol::ALL};
      static Endpoint From(const qrpc_endpoint_t &ep) {
        Endpoint e;
        e.host = ep.host ? ep.host : "";
        e.port = ep.port;
        e.path = ep.webrtc.path ? ep.webrtc.path : "/qrpc";
        e.protocol = static_cast<Port::Protocol>(ep.webrtc.proto);
        e.ip = ep.webrtc.ip ? ep.webrtc.ip : "";
        return e;
      }
    };    
    struct Config  {
      Resolver &resolver{NopResolver::Instance()};
      qrpc_time_t session_timeout, connection_timeout;
      bool in6{false}; // bind to IPv6 if true, otherwise IPv4
      // derived by other settings
      std::vector<std::string> ifaddrs;
    public:
      int Derive();
      // TODO: add certpair as its argument
      static Config From(
        Resolver &resolver, qrpc_time_t session_timeout, qrpc_time_t connection_timeout) {
        Config c;
        c.resolver = resolver;
        c.session_timeout = session_timeout;
        c.connection_timeout = connection_timeout;
        return c;
      }
      static Config From(
        qrpc_time_t session_timeout, qrpc_time_t connection_timeout) {
        Config c;
        c.session_timeout = session_timeout;
        c.connection_timeout = connection_timeout;
        return c;
      }
    };
    struct TransportConfig {
      rtp::Handler::Config rtp;
      qrpc_webrtc_params_config_t params;

      // might be derived from above config values
      MaybeCertPair certpair{std::nullopt};
      
      // derived from above config values
      std::string fingerprint, fingerprint_algorithm;
      std::vector<std::string> ice_candidate_addrs;
    public:
      static TransportConfig From(const qrpc_transport_config_t &conf) {
        TransportConfig c;
        if (conf.proto != QRPC_TRANSPORT_WEBTRANSPORT) {
          logger::die({{"ev","need webrtc config"},{"proto", conf.proto}});
        }
        auto &webrtc = conf.webrtc;
        c.params = webrtc.params;
        c.rtp = webrtc.rtp;
        return c;
      }
      int Derive(const Endpoint &ep, const ConnectionFactory::Config &conf);
    };    
  public: // connection
    class Connection;
    template <class PS>
    class TcpSessionTmpl : public PS {
    public:
      TcpSessionTmpl(TcpSessionFactory &f, Fd fd, const Address &addr, std::shared_ptr<Connection> c) :
        TcpSessionTmpl(f, fd, addr) { connection_ = c; }
      TcpSessionTmpl(TcpSessionFactory &f, Fd fd, const Address &addr) :
        PS(f, fd, addr), connection_() {}
      virtual ConnectionFactory &connection_factory() = 0;
      int OnRead(const char *p, size_t sz) override;
      qrpc_time_t OnShutdown() override;
    protected:
      std::shared_ptr<Connection> connection_;
    };
    typedef TcpSessionTmpl<TcpClient::TcpSession> TcpClientSession;
    typedef TcpSessionTmpl<TcpListener::TcpSession> TcpListenerSession;
    template <class PS>
    class UdpSessionTmpl : public PS {
    public:
      UdpSessionTmpl(UdpSessionFactory &f, Fd fd, const Address &addr, std::shared_ptr<Connection> c) :
        UdpSessionTmpl(f, fd, addr) { connection_ = c; }
      UdpSessionTmpl(UdpSessionFactory &f, Fd fd, const Address &addr) :
        PS(f, fd, addr), connection_() {}
      virtual ConnectionFactory &connection_factory() = 0;
      int OnRead(const char *p, size_t sz) override;
      qrpc_time_t OnShutdown() override;
    protected:
      std::shared_ptr<Connection> connection_;
    };
    typedef UdpSessionTmpl<UdpClient::UdpSession> UdpClientSession;
    typedef UdpSessionTmpl<UdpListener::UdpSession> UdpListenerSession;
    class SyscallStream : public AdhocStream {
    public:
      SyscallStream(BaseConnection &c, const Config &config, ConnectHandler &&h) :
        AdhocStream(c, config, Handler(Nop()), std::move(h), ShutdownHandler(Nop())) {}
      SyscallStream(BaseConnection &c, const Config &config) :
        AdhocStream(c, config, Handler(Nop()), ConnectHandler(Nop()), ShutdownHandler(Nop())) {}
      ~SyscallStream() {}
      int OnRead(const char *p, size_t sz) override;
      int Call(const char *fn, uint32_t msgid, const json &j, logger::level llv = logger::level::info);
      int Call(const char *fn, const json &j);
      int Call(const char *fn);
    };
    class SubscriberStream : public Stream {
    public:
      SubscriberStream(BaseConnection &c, const Config &config) : Stream(c, config) {}
      ~SubscriberStream() {}
      int OnRead(const char *p, size_t sz) override {
        return QRPC_OK;
      }
      void OnShutdown() override {
        auto &c = dynamic_cast<Connection &>(connection());
        QRPC_LOGJ(info, {{"ev","subscriber stream shutdown"},{"cname",c.cname()},{"label",label()},{"rtp_id",c.rtp_id()},{"ptr",str::dptr(this)}});
        c.rtp_handler().UnsubscribeStream(this);
        Stream::OnShutdown();
      }
    };
    class PublisherStream : public Stream {
    public:
      PublisherStream(BaseConnection &c, const std::shared_ptr<Stream> &published_stream) :
        Stream(c, published_stream->config()), published_stream_(published_stream) {
        published_stream_->SetPublished(true);
      }
      ~PublisherStream() { published_stream_->SetPublished(false); }
      std::shared_ptr<Stream> target() { return published_stream_; }
      int OnRead(const char *p, size_t sz) override {
        auto &c = dynamic_cast<Connection &>(connection());
        c.rtp_handler().EmitSubscribeStreams(published_stream_, p, sz);
        return published_stream_->OnRead(p, sz);
      }
      void OnShutdown() override {
        auto &c = dynamic_cast<Connection &>(connection());
        c.rtp_handler().UnpublishStream(published_stream_);
        published_stream_->OnShutdown();
      }
      void Close(const CloseReason &reason) override {
        published_stream_->Close(reason);
      }
    protected:
      std::shared_ptr<Stream> published_stream_;
    };
  public: // connections
    class Connection : public base::Connection, 
                       public IceServer::Listener,
                       public RTC::DtlsTransport::Listener,
                       public RTC::SctpAssociation::Listener,
                       public rtp::Handler::Listener {
    public:
      friend class ConnectionFactory;
    public:
      Connection(ConnectionFactory &sv, RTC::DtlsTransport::Role dtls_role) :
        factory_(sv), last_active_(qrpc_time_now()), dtls_role_(dtls_role) {
          // https://datatracker.ietf.org/doc/html/rfc8832#name-data_channel_open-message
          switch (dtls_role) {
            case RTC::DtlsTransport::Role::CLIENT:
              stream_id_factory_.configure(0, 2); // zero is allowed to be stream id of client
              break;
            case RTC::DtlsTransport::Role::SERVER:
              stream_id_factory_.configure(1, 2);
              break;
            default:
              DIE("invalid dtls role")
              break;          
          }
        }
      virtual ~Connection() {
        if (alarm_id_ != AlarmProcessor::INVALID_ID) {
          factory_.alarm_processor().Cancel(alarm_id_);
        }
      }
    public:
      // abstract methods
      virtual const TransportConfig &transport_config() const = 0;
      // methods depends on transport_config()
      const qrpc_webrtc_params_config_t &webrtc_params() const { return transport_config().params; }
      const std::string &fingerprint() const { return transport_config().fingerprint; }
      const std::string &fingerprint_algorithm() const { return transport_config().fingerprint_algorithm; }
      const std::vector<std::string> &ice_candidate_addrs() const { return transport_config().ice_candidate_addrs; }
    public:
      // implements base::Connection
      void Close() override;
      int Send(const char *p, size_t sz) override;
      int Send(Stream &s, const char *p, size_t sz, bool binary) override;
      void Close(Stream &s) override;
      int Open(Stream &s) override;
      std::shared_ptr<Stream> OpenStream(const Stream::Config &c) override {
        return OpenStream(c, stream_factory());
      }
      AlarmProcessor &alarm_processor() override { return factory().alarm_processor(); }
    public: // callbacks
      virtual int OnConnect() { return QRPC_OK; }
      virtual qrpc_time_t OnShutdown() { return 0; }
      virtual void OnFinalize();
    public:
      bool connected() const;
      inline bool closed() const { return closed_; }
      inline ConnectionFactory &factory() { return factory_; }
      inline const ConnectionFactory &factory() const { return factory_; }
      inline const IceServer &ice_server() const { return *ice_server_.get(); }
      inline const IceUFrag &ufrag() const { return ice_server().GetUsernameFragment(); }
      inline RTC::DtlsTransport &dtls_transport() { return *dtls_transport_.get(); }
      inline rtp::Handler &rtp_handler() { return *rtp_handler_.get(); }
      inline bool rtp_enabled() const { return rtp_handler_ != nullptr; }
      // for now, qrpc server initiates dtls transport because safari does not initiate it
      // even if we specify "setup: passive" in SDP of whip response
      inline bool is_client() const { return dtls_role_ == RTC::DtlsTransport::Role::SERVER; }
      inline bool is_consumer() const { 
        return std::find_if(media_stream_configs_.begin(), media_stream_configs_.end(), [](const auto &c) {
          return c.direction == rtp::MediaStreamConfig::Direction::SEND;
        }) != media_stream_configs_.end();
      }
      rtp::MediaStreamConfigs &media_stream_configs() override { return media_stream_configs_; }
    public:
      int Init(std::string &ufrag, std::string &pwd);
      void SetCname(const std::string &cname);
      bool SetRtpCapability(const std::string &cap_sdp, std::string &answer);
      void RegisterCname();
      void InitRTP();
      void Fin();
      void Touch(qrpc_time_t now) { last_active_ = now; }
      // first calling prepare consume to setup connection for consumer, then client connect to the connection, Consume starts actual rtp packet transfer
      bool PrepareConsume(
        const std::string &media_path, 
        const std::map<rtp::Parameters::MediaKind, ControlOptions> &options_map, bool sync,
        std::string &sdp, std::map<std::string,rtp::Consumer*> &created_consumers);
      bool ConsumeMedia(const rtp::MediaStreamConfig &config, std::string &error);
      bool Consume(std::map<std::string,rtp::Consumer*> &created_consumers, std::string &error);
      bool CloseMedia(const std::string &path, std::vector<std::string> &closed_paths, std::string &sdp_or_error);
      bool PublishStream(const std::string &path);
      bool UnpublishStream(const std::string &path);
      std::shared_ptr<Stream> SubscribeStream(const Stream::Config &c);
      inline void OnTimer(qrpc_time_t now) {}
      int RunDtlsTransport();
      IceProber *InitIceProber(const std::string &ufrag, const std::string &pwd, uint64_t priority);
      void OnDtlsEstablished();
      void OnTcpSessionShutdown(Session *s);
      void OnUdpSessionShutdown(Session *s);
      void TryParseRtcpPacket(const uint8_t *p, size_t sz);
      void TryParseRtpPacket(const uint8_t *p, size_t sz);
      std::shared_ptr<Stream> NewStream(const Stream::Config &c, const StreamFactory &sf);
      std::shared_ptr<Stream> OpenStream(const Stream::Config &c, const StreamFactory &sf);
      StreamFactory DefaultStreamFactory();
      bool Timeout(qrpc_time_t now, qrpc_time_t timeout, qrpc_time_t &next_check) const {
        return Session::CheckTimeout(last_active_, now, timeout, next_check);
      }
    public:
      // entry point of all incoming packets
      int OnPacketReceived(Session *session, const uint8_t *p, size_t sz);
      // protocol handlers
      int OnStunDataReceived(Session *session, const uint8_t *p, size_t sz);
      int OnDtlsDataReceived(Session *session, const uint8_t *p, size_t sz);
      int OnRtcpDataReceived(Session *session, const uint8_t *p, size_t sz);
      int OnRtpDataReceived(Session *session, const uint8_t *p, size_t sz);      
    public:
      // implements IceServer::Listener
			void OnIceServerSendStunPacket(
			  const IceServer *iceServer, const RTC::StunPacket* packet, Session *session) override;
			void OnIceServerLocalUsernameFragmentAdded(
			  const IceServer *iceServer, const std::string& usernameFragment) override;
			void OnIceServerLocalUsernameFragmentRemoved(
			  const IceServer *iceServer, const std::string& usernameFragment) override;
			void OnIceServerSessionAdded(const IceServer *iceServer, Session *session) override;
			void OnIceServerSessionRemoved(
			  const IceServer *iceServer, Session *session) override;
			void OnIceServerSelectedSession(
			  const IceServer *iceServer, Session *session)        override;
			void OnIceServerConnected(const IceServer *iceServer)    override;
			void OnIceServerCompleted(const IceServer *iceServer)    override;
			void OnIceServerDisconnected(const IceServer *iceServer) override;
      bool OnIceServerCheckClosed(const IceServer *) override { return closed(); }
			void OnIceServerSuccessResponded(
					const IceServer *iceServer, const RTC::StunPacket* packet, Session *session) override;
			void OnIceServerErrorResponded(
				const IceServer *iceServer, const RTC::StunPacket* packet, Session *session) override;

      // implements RTC::DtlsTransport::Listener
			void OnDtlsTransportConnecting(const RTC::DtlsTransport* dtlsTransport) override;
			void OnDtlsTransportConnected(
			  const RTC::DtlsTransport* dtlsTransport,
			  RTC::SrtpSession::CryptoSuite srtpCryptoSuite,
			  uint8_t* srtpLocalKey,
			  size_t srtpLocalKeyLen,
			  uint8_t* srtpRemoteKey,
			  size_t srtpRemoteKeyLen,
			  std::string& remoteCert) override;
			// The DTLS connection has been closed as the result of an error (such as a
			// DTLS alert or a failure to validate the remote fingerprint).
			void OnDtlsTransportFailed(const RTC::DtlsTransport* dtlsTransport) override;
			// The DTLS connection has been closed due to receipt of a close_notify alert.
			void OnDtlsTransportClosed(const RTC::DtlsTransport* dtlsTransport) override;
			// Need to send DTLS data to the peer.
			void OnDtlsTransportSendData(
			  const RTC::DtlsTransport* dtlsTransport, const uint8_t* data, size_t len) override;
			// DTLS application data received.
			void OnDtlsTransportApplicationDataReceived(
			  const RTC::DtlsTransport* dtlsTransport, const uint8_t* data, size_t len) override;  

      // implements SctpAssociation::Listener
			void OnSctpAssociationConnecting(RTC::SctpAssociation* sctpAssociation) override;
			void OnSctpAssociationConnected(RTC::SctpAssociation* sctpAssociation)  override;
			void OnSctpAssociationFailed(RTC::SctpAssociation* sctpAssociation)     override;
			void OnSctpAssociationClosed(RTC::SctpAssociation* sctpAssociation)     override;
			void OnSctpAssociationSendData(
			  RTC::SctpAssociation* sctpAssociation, const uint8_t* data, size_t len) override;
			void OnSctpStreamReset(
			  RTC::SctpAssociation* sctpAssociation, uint16_t streamId) override;        
			void OnSctpWebRtcDataChannelControlDataReceived(
			  RTC::SctpAssociation* sctpAssociation,
			  uint16_t streamId,
			  const uint8_t* msg,
			  size_t len) override;
			void OnSctpAssociationMessageReceived(
			  RTC::SctpAssociation* sctpAssociation,
			  uint16_t streamId,
			  const uint8_t* msg,
			  size_t len, uint32_t ppid) override;
			void OnSctpAssociationBufferedAmount(
			  RTC::SctpAssociation* sctpAssociation, uint32_t len) override;

      // implements rtp::Handler::Listener
      const std::string &rtp_id() const override { return ufrag(); }
      const std::string &cname() const override { return cname_; }
      const std::map<rtp::Parameters::MediaKind, rtp::Capability> &
        capabilities() const override { return capabilities_; }
      const std::string &FindRtpIdFrom(std::string &cname) override;
      const std::string GenerateMid() override {
        auto mid = mid_seed_++;
        if (mid_seed_ > 1000000000) { ASSERT(false); mid_seed_ = 0; } 
        return std::to_string(mid);
      }
      int SendToStream(const std::string &label, const char *data, size_t len) override;      
      void RecvStreamClosed(uint32_t ssrc) override;
      void SendStreamClosed(uint32_t ssrc) override; 
      bool IsConnected() const override;
      void SendRtpPacket(
        RTC::Consumer* consumer, RTC::RtpPacket* packet, onSendCallback* cb = nullptr) override;
      void SendRtcpPacket(RTC::RTCP::Packet* packet) override;
      void SendRtcpCompoundPacket(RTC::RTCP::CompoundPacket* packet) override;
      void SendMessage(
        RTC::DataConsumer* dataConsumer,
        const uint8_t* msg,
        size_t len,
        uint32_t ppid,
        rtp::Handler::QueueCB* = nullptr) override { ASSERT(false); }
      void SendSctpData(const uint8_t* data, size_t len) override { ASSERT(false); }
      const rtp::Handler::Config &GetRtpConfig() const override { return transport_config().rtp; }
      bool GetRtpRoc(uint32_t ssrc, uint32_t &roc, rtp::MediaStreamConfig::Direction dir) override;
    protected:
      ConnectionFactory &factory_;
      qrpc_time_t last_active_, start_shutdown_{0};
      std::unique_ptr<IceServer> ice_server_; // ICE
      std::unique_ptr<IceProber> ice_prober_; // ICE(client)
      RTC::DtlsTransport::Role dtls_role_;
      std::unique_ptr<RTC::DtlsTransport> dtls_transport_; // DTLS
      std::unique_ptr<RTC::SctpAssociation> sctp_association_; // SCTP
      std::unique_ptr<RTC::SrtpSession> srtp_send_, srtp_recv_; // SRTP
      std::string srtp_remote_key_;
      RTC::SrtpSession::CryptoSuite srtp_crypto_suite_{RTC::SrtpSession::CryptoSuite::AES_CM_128_HMAC_SHA1_80};
      std::shared_ptr<rtp::Handler> rtp_handler_; // RTP, RTCP
      std::map<Stream::Id, std::shared_ptr<Stream>> streams_;
      std::shared_ptr<SyscallStream> syscall_;
      IdFactory<Stream::Id> stream_id_factory_;
      AlarmProcessor::Id alarm_id_{AlarmProcessor::INVALID_ID};
      std::string cname_;
      std::map<rtp::Parameters::MediaKind, rtp::Capability> capabilities_;
      rtp::MediaStreamConfigs media_stream_configs_; // stream configs with keeping creation order
      uint32_t mid_seed_{0};
      bool sctp_connected_{false}, closed_{false};
      std::unique_ptr<CloseReason> close_reason_;
    };
  public:
    ConnectionFactory(Loop &l, Config &&config) :
      loop_(l), config_(std::move(config)), connections_() { Init(); }
    virtual ~ConnectionFactory() { Fin(); }
  public:
    Loop &loop() { return loop_; }
    const Config &config() const { return config_; }
    AlarmProcessor &alarm_processor() { return loop_.alarm_processor(); }
    template <class F> inline F& to() { return reinterpret_cast<F &>(*this); }
    template <class F> inline const F& to() const { return reinterpret_cast<const F &>(*this); }
  public:
    virtual bool is_client() const = 0;
    virtual int Setup(const std::vector<Port> &ports) = 0;
  public:
    int Init();
    void Fin();
    int Start(const std::vector<Port> &ports);
    std::shared_ptr<rtp::Handler> FindHandler(const std::string &cname);
    std::shared_ptr<Connection> FindFromUfrag(const IceUFrag &ufrag);
    std::shared_ptr<Connection> FindFromStunRequest(const uint8_t *p, size_t sz);
    inline void ScheduleClose(Connection &c, qrpc_close_reason_code_t code,
      int64_t detail_code = 0, const std::string &msg = "") {
      ScheduleClose(c, CloseReason{ .code = code, .detail_code = detail_code, .msg = msg });
    }
    void ScheduleClose(Connection &c, const CloseReason &cr) {
      if (c.closed_) { return; }
      c.closed_ = true;
      c.close_reason_ = std::make_unique<CloseReason>(cr);
      c.start_shutdown_ = qrpc_time_now();
      c.alarm_id_ = alarm_processor().Set([this, &c]() {
        // wait for sending all buffered data to peer
        if (c.sctp_association_->GetSctpBufferedAmount() > 0) {
          if (c.start_shutdown_ + c.webrtc_params().shutdown_timeout > qrpc_time_now()) {
            return qrpc_time_now();
          } // if shutdown_timeout duration passed, force close the connection
        }
        c.alarm_id_ = AlarmProcessor::INVALID_ID; // prevent AlarmProcessor::Cancel to be called
        CloseConnection(c);
        return qrpc_alarm_stop_rv(); // because this return value stops the alarm
      }, qrpc_time_now());
    }
    void ScheduleClose(const IceUFrag &ufrag, qrpc_close_reason_code_t code,
      int64_t detail_code = 0, const std::string &msg = "") {
      auto it = connections_.find(ufrag);
      if (it != connections_.end()) {
        ScheduleClose(*it->second, code, detail_code, msg);
      }
    }
  protected:
    void RegisterCname(const std::string &cname, std::shared_ptr<Connection> &c);
    std::shared_ptr<Connection> Create(
      RTC::DtlsTransport::Role dtls_role, std::string &ufrag, std::string &pwd,
      FactoryMethod &fm
    );
    void CloseConnection(Connection &c);
    qrpc_time_t CheckTimeout() {
        qrpc_time_t now = qrpc_time_now();
        // at least, the check will be done with default connection_timeout duration later
        qrpc_time_t nearest_check = now + config().connection_timeout;
        for (auto s = connections_.begin(); s != connections_.end();) {
            qrpc_time_t next_check;
            auto cur = s++;
            auto &c = *cur->second;
            if (c.closed_) { continue; } // wait for scheduleclose done
            if (c.Timeout(now, c.webrtc_params().connection_timeout, next_check)) {
                // inside CloseConnection, the entry will be erased
                ScheduleClose(c, QRPC_CLOSE_REASON_TIMEOUT);
            } else {
                nearest_check = std::min(nearest_check, next_check);
            }
        }
        return nearest_check;
    }
  protected:
    Loop &loop_;
    Config config_;
    AlarmProcessor::Id alarm_id_{AlarmProcessor::INVALID_ID};
    std::map<IceUFrag, std::shared_ptr<Connection>> connections_;
    std::map<std::string, std::shared_ptr<Connection>> cnmap_;
  private:
    static int32_t g_ref_count_;
    static thread_local int32_t g_thread_ref_count_;
    static std::mutex g_ref_sync_mutex_;
    static int ThreadInit(AlarmProcessor &a);
    static void ThreadFin(AlarmProcessor &a);
    static int GlobalInit();
    static void GlobalFin();
  };

  // TODO: use concept
  template <class CONNLIKE>
  class AdhocConnection : public CONNLIKE {
  public:
    typedef ConnectionFactory::CloseReason CloseReason;
    typedef std::function<int (CONNLIKE &)> ConnectHandler;
    typedef std::function<qrpc_time_t (CONNLIKE &, const CloseReason &)> ShutdownHandler;
    struct Nop {
      int operator()(CONNLIKE &) { return QRPC_OK; }
      qrpc_time_t operator()(CONNLIKE &, const CloseReason &) { return qrpc_alarm_stop_rv(); }
    };
  public:
    template <class... BaseArgs,
      class = std::enable_if_t<std::is_constructible<CONNLIKE, ConnectionFactory &, RTC::DtlsTransport::Role, BaseArgs...>::value>>  
    AdhocConnection(ConnectionFactory &sv, RTC::DtlsTransport::Role dtls_role, ConnectHandler &&ch, ShutdownHandler &&sh, BaseArgs&&... base_args) :
      CONNLIKE(sv, dtls_role, std::forward<BaseArgs>(base_args)...), connect_handler_(std::move(ch)), shutdown_handler_(std::move(sh)) {}
    int OnConnect() override { return connect_handler_(*this); }
    qrpc_time_t OnShutdown() override { return shutdown_handler_(*this, *CONNLIKE::close_reason_); }
  private:
    ConnectHandler connect_handler_;
    ShutdownHandler shutdown_handler_;
  };


  // Client
  class Client : public ConnectionFactory {
  public:
    class TcpClient : public base::TcpClient {
    public:
      TcpClient(ConnectionFactory &cf) :
        base::TcpClient(cf.loop(), cf.config().resolver, cf.config().session_timeout), cf_(cf) {}
      ConnectionFactory &connection_factory() { return cf_; }
    private:
      ConnectionFactory &cf_;
    };
    class TcpSession : public ConnectionFactory::TcpClientSession {
    public:
      typedef TcpClient Factory;
      TcpSession(TcpClient &f, Fd fd, const Address &addr, std::shared_ptr<Connection> c) :
        ConnectionFactory::TcpClientSession(f, fd, addr, c) {}
      ConnectionFactory &connection_factory() override { return factory().to<TcpClient>().connection_factory(); }
    };
    class UdpClient : public base::UdpClient {
    public:
      UdpClient(ConnectionFactory &cf) :
        base::UdpClient(cf.loop(), cf.config().resolver, cf.config().session_timeout), cf_(cf) {}
      ConnectionFactory &connection_factory() { return cf_; }
    private:
      ConnectionFactory &cf_;
    };
    class UdpSession : public ConnectionFactory::UdpClientSession {
    public:
      typedef UdpClient Factory;
      UdpSession(UdpClient &f, Fd fd, const Address &addr, std::shared_ptr<Connection> c) :
        ConnectionFactory::UdpClientSession(f, fd, addr, c) {}
      ConnectionFactory &connection_factory() override { return factory().to<UdpClient>().connection_factory(); }
    };
    class Connection : public ConnectionFactory::Connection {
    public:
      Connection(
        ConnectionFactory &sv, RTC::DtlsTransport::Role dtls_role, 
        TransportConfig &&tc, StreamFactory &&sf
      ) : ConnectionFactory::Connection(sv, dtls_role), 
        transport_config_(std::move(tc)), factory_method_(), stream_factory_(std::move(sf)) {}
      ~Connection() override {}
      void SetFactoryMethod(FactoryMethod &&fm) { factory_method_ = std::move(fm); }
    public:
      inline FactoryMethod &&factory_method() { return std::move(factory_method_); }
      StreamFactory &stream_factory() override { return stream_factory_; }
      const TransportConfig &transport_config() const override { return transport_config_; }
      TransportConfig &transport_config() { return transport_config_; }
    protected:
      TransportConfig transport_config_;
      FactoryMethod factory_method_; // for reconnecting
      StreamFactory stream_factory_;
    };
  public:
    Client(Loop &l, Config &&config) :
      ConnectionFactory(l, std::move(config)),
      http_client_(l, config.resolver, CertificatePair::Default()) {}
    Client(Loop &l, Config &config) :
      Client(l, std::move(config)) {}
    ~Client() override { Fin(); }
  public:
    std::map<IceUFrag, Endpoint> &endpoints() { return endpoints_; }
  public:
    bool Connect(const Endpoint &ep, FactoryMethod &&fm);
    void Close(BaseConnection &c) { ScheduleClose(dynamic_cast<Connection &>(c), QRPC_CLOSE_REASON_LOCAL); }
    void Fin();
    // implement ConnectionFactory
    virtual bool is_client() const override { return true; }
    virtual int Setup(const std::vector<Port> &ports) override;
  public:
    int Offer(const Endpoint &ep, const IceUFrag &ufrag, const std::string &pwd, std::string &sdp);
    bool Open(
      const Endpoint &ep, const std::vector<Candidate> &candidate, 
      size_t idx, std::shared_ptr<ConnectionFactory::Connection> &c
    );
  protected:
    HttpClient http_client_;
    std::map<IceUFrag, Endpoint> endpoints_;
    std::vector<TcpClient> tcp_clients_;
    std::vector<UdpClient> udp_clients_;
  };


  // AdhocClient
  class AdhocClient : public Client {
  public:
    typedef AdhocConnection<Connection> AdhocConnection;
    typedef AdhocConnection::ConnectHandler ConnectHandler;
    typedef AdhocConnection::ShutdownHandler ShutdownHandler;
  public:
    AdhocClient(Loop &l, Config &&c, Stream::Handler &&h) : Client(l, std::move(c)), stream_handler_(std::move(h)) {}
    AdhocClient(Loop &l, Config &&c, Stream::Handler &&h, AdhocStream::ConnectHandler &&ch, AdhocStream::ShutdownHandler &&sh) :
      Client(l, std::move(c)), stream_handler_(std::move(h)),
      stream_connect_handler_(std::move(ch)), stream_shutdown_handler_(std::move(sh)) {}
    AdhocClient(Loop &l, Config &&c, AdhocConnection::ConnectHandler &&cch, AdhocConnection::ShutdownHandler &&csh,
      Stream::Handler &&h, AdhocStream::ConnectHandler &&ch, AdhocStream::ShutdownHandler &&sh) :
      Client(l, std::move(c)), connect_handler_(std::move(cch)), shutdown_handler_(std::move(csh)),
      stream_handler_(std::move(h)), stream_connect_handler_(std::move(ch)), stream_shutdown_handler_(std::move(sh)) {}
    ~AdhocClient() override {}
  public:
    bool Connect(const Endpoint &ep, const TransportConfig &&transport_config) {
      return Client::Connect(ep, [this, tc = std::forward<const TransportConfig>(transport_config)](
        ConnectionFactory &cf, RTC::DtlsTransport::Role dtls_role
      ) mutable {
        auto ch = connect_handler_;
        auto sh = shutdown_handler_;
        return new AdhocConnection(cf, dtls_role, std::move(ch), std::move(sh), std::move(tc), [this](
          const Stream::Config &c, base::Connection &conn
        ) {
          // make copy of handlers here, all are move into the new AdhocStream
          auto sh = stream_handler_;
          auto sch = stream_connect_handler_;
          auto ssh = stream_shutdown_handler_;
          return std::shared_ptr<Stream>(new AdhocStream(conn, c, std::move(sh), std::move(sch), std::move(ssh)));
        });
      });
    }
  protected:
    AdhocConnection::ConnectHandler connect_handler_;
    AdhocConnection::ShutdownHandler shutdown_handler_;
    Stream::Handler stream_handler_;
    AdhocStream::ConnectHandler stream_connect_handler_;
    AdhocStream::ShutdownHandler stream_shutdown_handler_;
  };


  // Listener
  class Listener : public ConnectionFactory {
  public:
    class TcpSession : public ConnectionFactory::TcpListenerSession {
    public:
      TcpSession(TcpListener &f, Fd fd, const Address &addr) :
        ConnectionFactory::TcpListenerSession(f, fd, addr) {}
      ConnectionFactory &connection_factory() override;
    };
    typedef TcpListenerOf<TcpSession> TcpPortBase;
    class TcpPort : public TcpPortBase {
    public:
      TcpPort(ConnectionFactory &cf) : TcpPortBase(cf.loop(), cf.to<Listener>().tcp_listener_config()), cf_(cf) {}
      ConnectionFactory &connection_factory() { return cf_; }
    private:
      ConnectionFactory &cf_;
    };
    class UdpSession : public ConnectionFactory::UdpListenerSession {
    public:
      UdpSession(UdpListener &f, Fd fd, const Address &addr) :
        ConnectionFactory::UdpListenerSession(f, fd, addr) {}
      ConnectionFactory &connection_factory() override;
    };
    typedef UdpListenerOf<UdpSession> UdpPortBase;
    class UdpPort : public UdpPortBase {
    public:
      UdpPort(ConnectionFactory &cf) : UdpPortBase(cf.loop(), cf.to<Listener>().udp_listener_config()), cf_(cf) {
        ASSERT(&alarm_processor_ != &NopAlarmProcessor::Instance());
      }
      ConnectionFactory &connection_factory() { return cf_; }
    private:
      ConnectionFactory &cf_;
    };
    class Connection : public ConnectionFactory::Connection {
    public:
      Connection(ConnectionFactory &sv, RTC::DtlsTransport::Role dtls_role) :
        ConnectionFactory::Connection(sv, dtls_role) {}
      ~Connection() override {}
      StreamFactory &stream_factory() override {
        return factory().to<base::webrtc::Listener>().stream_factory();
      }
      const TransportConfig &transport_config() const override {
        return factory().to<base::webrtc::Listener>().transport_config();
      }
    };
  public:
    Listener(Loop &l, Config &&config, TransportConfig &&tc, FactoryMethod &&fm, StreamFactory &&sf) :
      ConnectionFactory(l, std::move(config)), factory_method_(std::move(fm)), stream_factory_(std::move(sf)),
      transport_config_(std::move(tc)), http_listener_(l, http_listener_config()) {}
    ~Listener() override { Fin(); }
  public:
    uint16_t udp_port() const { return udp_ports_.empty() ? 0 : udp_ports_[0].port(); }
    uint16_t tcp_port() const { return tcp_ports_.empty() ? 0 : tcp_ports_[0].port(); }
    const UdpListener::Config udp_listener_config() const {
      auto &params = transport_config_.params;
      return UdpListener::Config(config_.resolver, params.session_timeout, params.udp_batch_size, false);
    }
    const TcpListener::Config tcp_listener_config() const {
      auto &params = transport_config_.params;
      return TcpListener::Config(config_.resolver, params.session_timeout, transport_config().certpair);
    }
    const TcpListener::Config http_listener_config() const {
      return TcpListener::Config(config_.resolver, webrtc_params().http_timeout, transport_config().certpair);
    }
  public:
    int Accept(const std::string &client_req_body, json &response);
    void Close(BaseConnection &c) { ScheduleClose(dynamic_cast<Connection &>(c), QRPC_CLOSE_REASON_LOCAL); }
    void Fin();
    bool Listen(int signaling_port, const Endpoint &ep);
    inline bool Listen(int signaling_port, int port) {
      return Listen(signaling_port, Endpoint{
        .port = port
      });
    }
    HttpRouter &http_router() { return router_; }
    StreamFactory &stream_factory() { return stream_factory_; }
    const TransportConfig &transport_config() const { return transport_config_; }
    const qrpc_webrtc_params_config_t &webrtc_params() const { return transport_config().params; }
    // implement ConnectionFactory
    virtual bool is_client() const override { return false; }
    virtual int Setup(const std::vector<Port> &ports) override;
  protected:
    HttpListener http_listener_;
    HttpRouter router_;
    FactoryMethod factory_method_;
    StreamFactory stream_factory_;
    TransportConfig transport_config_;
    std::vector<TcpPort> tcp_ports_;
    std::vector<UdpPort> udp_ports_;
  };


  // AdhocListener
  class AdhocListener : public Listener {
  public:
    typedef AdhocConnection<Connection> AdhocConnection;
    typedef AdhocConnection::ConnectHandler ConnectHandler;
    typedef AdhocConnection::ShutdownHandler ShutdownHandler;
  public:
    AdhocListener(Loop &l, Config &&c, TransportConfig &&tc, Stream::Handler &&h) :
      AdhocListener(l, std::move(c), std::move(tc), std::move(h), AdhocStream::Nop(), AdhocStream::Nop()) {}
    AdhocListener(Loop &l, Config &&c, TransportConfig &&tc,
      Stream::Handler &&h, AdhocStream::ConnectHandler &&ch, AdhocStream::ShutdownHandler &&sh) :
      AdhocListener(l, std::move(c), std::move(tc), std::move(h), AdhocConnection::Nop(), AdhocConnection::Nop(), 
        std::move(ch), std::move(sh)) {}
    AdhocListener(Loop &l, Config &&c, TransportConfig &&tc, Stream::Handler &&h, 
      AdhocConnection::ConnectHandler &&cch, AdhocConnection::ShutdownHandler &&csh,
      AdhocStream::ConnectHandler &&ch, AdhocStream::ShutdownHandler &&sh) :
      Listener(l, std::move(c), std::move(tc), [this](ConnectionFactory &cf, RTC::DtlsTransport::Role role) {
        auto cchh = connect_handler_; auto cshh = shutdown_handler_;
        return new AdhocConnection(cf, role, std::move(cchh), std::move(cshh));
      }, [h = std::move(h), ch = std::move(ch), sh = std::move(sh)](
        const Stream::Config &config, base::Connection &conn
      ) {
        auto hh = h; auto chh = ch; auto shh = sh;
        return std::shared_ptr<Stream>(new AdhocStream(conn, config, std::move(hh), std::move(chh), std::move(shh)));
      }), connect_handler_(std::move(cch)), shutdown_handler_(std::move(csh)) {}
    ~AdhocListener() override {}
  protected:
    AdhocConnection::ConnectHandler connect_handler_;
    AdhocConnection::ShutdownHandler shutdown_handler_;
  };  
  typedef ConnectionFactory::Connection Connection;
} //namespace webrtc
} //namespace base