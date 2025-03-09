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
        AdhocStream(c, config, std::move(Handler(Nop())), std::move(h), std::move(ShutdownHandler(Nop()))) {}
      SyscallStream(BaseConnection &c, const Config &config) :
        AdhocStream(c, config, std::move(Handler(Nop())), std::move(ConnectHandler(Nop())), std::move(ShutdownHandler(Nop()))) {}
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
        factory_(sv), last_active_(qrpc_time_now()), ice_server_(nullptr), dtls_role_(dtls_role),
        dtls_transport_(nullptr), sctp_association_(nullptr), srtp_send_(nullptr), srtp_recv_(nullptr),
        rtp_handler_(nullptr), streams_(), syscall_(), stream_id_factory_(),
        alarm_id_(AlarmProcessor::INVALID_ID), mid_seed_(0), sctp_connected_(false), closed_(false) {
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
      // implements base::Connection
      void Close() override;
      int Send(const char *p, size_t sz) override;
      int Send(Stream &s, const char *p, size_t sz, bool binary) override;
      void Close(Stream &s) override;
      int Open(Stream &s) override;
      std::shared_ptr<Stream> OpenStream(const Stream::Config &c) override {
        return OpenStream(c, factory().stream_factory());
      }
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
      const rtp::Handler::Config &GetRtpConfig() const override { return factory().config().rtp; }
    protected:
      qrpc_time_t last_active_;
      ConnectionFactory &factory_;
      std::unique_ptr<IceServer> ice_server_; // ICE
      std::unique_ptr<IceProber> ice_prober_; // ICE(client)
      RTC::DtlsTransport::Role dtls_role_;
      std::unique_ptr<RTC::DtlsTransport> dtls_transport_; // DTLS
      std::unique_ptr<RTC::SctpAssociation> sctp_association_; // SCTP
      std::unique_ptr<RTC::SrtpSession> srtp_send_, srtp_recv_; // SRTP
      std::shared_ptr<rtp::Handler> rtp_handler_; // RTP, RTCP
      std::map<Stream::Id, std::shared_ptr<Stream>> streams_;
      std::shared_ptr<SyscallStream> syscall_;
      IdFactory<Stream::Id> stream_id_factory_;
      AlarmProcessor::Id alarm_id_;
      std::string cname_;
      std::map<rtp::Parameters::MediaKind, rtp::Capability> capabilities_;
      rtp::MediaStreamConfigs media_stream_configs_; // stream configs with keeping creation order
      uint32_t mid_seed_;
      bool sctp_connected_, closed_;
    };
    typedef std::function<Connection *(ConnectionFactory &, RTC::DtlsTransport::Role)> FactoryMethod;
    struct Port {
      enum Protocol {
        NONE = 0,
        UDP = 1,
        TCP = 2,
      };
      Protocol protocol;
      int port;
    };
    struct Config {
      std::string ip;
      std::vector<Port> ports;
      rtp::Handler::Config rtp;
      size_t max_outgoing_stream_size, initial_incoming_stream_size;
      size_t send_buffer_size, udp_batch_size;
      qrpc_time_t session_timeout, http_timeout;
      qrpc_time_t connection_timeout, consent_check_interval;
      std::string fingerprint_algorithm;
      bool in6{false};
      Resolver &resolver{NopResolver::Instance()};

      // derived from above config values
      std::string fingerprint;
      std::vector<std::string> ifaddrs;
    public:
      int Derive();
    };
  public:
    ConnectionFactory(Loop &l, Config &&config, FactoryMethod &&fm, StreamFactory &&sf) :
      loop_(l), config_(config), factory_method_(fm), stream_factory_(sf), connections_() {}
    ConnectionFactory(Loop &l, Config &&config, StreamFactory &&sf) :
      loop_(l), config_(config), factory_method_([](ConnectionFactory &cf, RTC::DtlsTransport::Role role) {
        return new Connection(cf, role);
      }), stream_factory_(sf), connections_() {}
    virtual ~ConnectionFactory() { Fin(); }
  public:
    Loop &loop() { return loop_; }
    const Config &config() const { return config_; }
    StreamFactory &stream_factory() { return stream_factory_; }
    AlarmProcessor &alarm_processor() { return loop_.alarm_processor(); }
    Resolver &resolver() { return config_.resolver; }
    template <class F> inline F& to() { return reinterpret_cast<F &>(*this); }
    template <class F> inline const F& to() const { return reinterpret_cast<const F &>(*this); }
    const std::string &fingerprint() const { return config_.fingerprint; }
    const std::string &fingerprint_algorithm() const { return config_.fingerprint_algorithm; }
    const UdpListener::Config udp_listener_config() const {
      return UdpListener::Config(config_.resolver, config_.session_timeout, config_.udp_batch_size, false);
    }
    const TcpListener::Config http_listener_config() const {
      return TcpListener::Config(config_.resolver, config_.http_timeout);
    }
    const std::string primary_proto() const {
      return config_.ports[0].protocol == Port::Protocol::UDP ? "UDP" : "TCP";
    }
  public:
    virtual bool is_client() const = 0;
    virtual int Setup() = 0;
  public:
    int Init();
    void Fin();
    std::shared_ptr<rtp::Handler> FindHandler(const std::string &cname);
    std::shared_ptr<Connection> FindFromUfrag(const IceUFrag &ufrag);
    std::shared_ptr<Connection> FindFromStunRequest(const uint8_t *p, size_t sz);
    void ScheduleClose(Connection &c) {
      if (c.closed_) { return; }
      c.closed_ = true;
      c.alarm_id_ = alarm_processor().Set([this, &c]() {
        c.alarm_id_ = AlarmProcessor::INVALID_ID; // prevent AlarmProcessor::Cancel to be called
        CloseConnection(c);
        return 0; // because this return value stops the alarm
      }, qrpc_time_now());
    }
    void ScheduleClose(const IceUFrag &ufrag) {
      auto it = connections_.find(ufrag);
      if (it != connections_.end()) {
        ScheduleClose(*it->second);
      }
    }
  protected:
    void RegisterCname(const std::string &cname, std::shared_ptr<Connection> &c);
    std::shared_ptr<Connection> Create(
      RTC::DtlsTransport::Role dtls_role, std::string &ufrag, std::string &pwd, bool do_entry = false);
    void CloseConnection(Connection &c);
    void CloseConnection(const IceUFrag &ufrag) {
      auto it = connections_.find(ufrag);
      if (it != connections_.end()) {
        CloseConnection(*it->second);
      }
    }
    qrpc_time_t CheckTimeout() {
        qrpc_time_t now = qrpc_time_now();
        qrpc_time_t nearest_check = now + config_.connection_timeout;
        for (auto s = connections_.begin(); s != connections_.end();) {
            qrpc_time_t next_check;
            auto cur = s++;
            if (cur->second->Timeout(now, config_.connection_timeout, next_check)) {
                // inside CloseConnection, the entry will be erased
                CloseConnection(*cur->second);
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
    FactoryMethod factory_method_;
    StreamFactory stream_factory_;
    std::map<IceUFrag, std::shared_ptr<Connection>> connections_;
    std::map<std::string, std::shared_ptr<Connection>> cnmap_;
  private:
    static uint32_t g_ref_count_;
    static std::mutex g_ref_sync_mutex_;
    static int GlobalInit(AlarmProcessor &a);
    static void GlobalFin();
  };
  class AdhocConnection : public ConnectionFactory::Connection {
  public:
    typedef std::function<int (ConnectionFactory::Connection &)> ConnectHandler;
    typedef std::function<qrpc_time_t (ConnectionFactory::Connection &)> ShutdownHandler;
  public:
    AdhocConnection(ConnectionFactory &sv, RTC::DtlsTransport::Role dtls_role, ConnectHandler &&ch, ShutdownHandler &&sh) :
      Connection(sv, dtls_role), connect_handler_(std::move(ch)), shutdown_handler_(std::move(sh)) {};
    int OnConnect() override { return connect_handler_(*this); }
    qrpc_time_t OnShutdown() override { return shutdown_handler_(*this); }
  private:
    ConnectHandler connect_handler_;
    ShutdownHandler shutdown_handler_;
  };


  // Client
  class Client : public ConnectionFactory {
  public:
    struct Endpoint {
      std::string host, path;
      int port;
    };
  public:
    class TcpClient : public base::TcpClient {
    public:
      TcpClient(ConnectionFactory &cf) :
        base::TcpClient(cf.loop(), cf.resolver(), cf.config().session_timeout), cf_(cf) {}
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
        base::UdpClient(cf.loop(), cf.resolver(), cf.config().session_timeout), cf_(cf) {}
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
  public:
    Client(Loop &l, Config &&config, StreamFactory &&sf) :
      ConnectionFactory(l, std::move(config), std::move(sf)), http_client_(l, config.resolver),
      tcp_clients_(), udp_clients_() {}
    Client(Loop &l, Config &&config, FactoryMethod &&fm, StreamFactory &&sf) :
      ConnectionFactory(l, std::move(config), std::move(fm), std::move(sf)), http_client_(l, config.resolver),
      tcp_clients_(), udp_clients_() {}
    ~Client() override { Fin(); }
  public:
    std::map<IceUFrag, Endpoint> &endpoints() { return endpoints_; }
  public:
    bool Connect(const std::string &host, int port, const std::string &path = "/qrpc");
    void Close(BaseConnection &c) { CloseConnection(dynamic_cast<Connection &>(c)); }
    void Fin();
    // implement ConnectionFactory
    virtual bool is_client() const override { return true; }
    virtual int Setup() override;
  public:
    int Offer(const Endpoint &ep, std::string &sdp, std::string &ufrag);
    bool Open(const std::vector<Candidate> &candidate, size_t idx, std::shared_ptr<Connection> &c);
  protected:
    HttpClient http_client_;
    std::map<IceUFrag, Endpoint> endpoints_;
    std::vector<TcpClient> tcp_clients_;
    std::vector<UdpClient> udp_clients_;
  };


  // AdhocClient
  class AdhocClient : public Client {
  public:
    AdhocClient(Loop &l, Config &&c, Stream::Handler &&h) : Client(l, std::move(c), 
      [h = std::move(h)](const Stream::Config &config, base::Connection &conn) {
        auto hh = h;
        return std::shared_ptr<Stream>(new AdhocStream(conn, config, std::move(hh)));
      }) {}
    AdhocClient(Loop &l, Config &&c,
      Stream::Handler &&h, AdhocStream::ConnectHandler &&ch, AdhocStream::ShutdownHandler &&sh) :
      Client(l, std::move(c), [h = std::move(h), ch = std::move(ch), sh = std::move(sh)](
        const Stream::Config &config, base::Connection &conn
      ) {
        auto hh = h; auto chh = ch; auto shh = sh;
        return std::shared_ptr<Stream>(new AdhocStream(conn, config, std::move(hh), std::move(chh), std::move(shh)));
      }) {}
    AdhocClient(Loop &l, Config &&c, 
      AdhocConnection::ConnectHandler &&cch, AdhocConnection::ShutdownHandler &&csh,
      Stream::Handler &&h, AdhocStream::ConnectHandler &&ch, AdhocStream::ShutdownHandler &&sh) :
      Client(l, std::move(c), [cch = std::move(cch), csh = std::move(csh)](ConnectionFactory &cf, RTC::DtlsTransport::Role role) {
        auto cchh = cch; auto cshh = csh;
        return new AdhocConnection(cf, role, std::move(cchh), std::move(cshh));
      }, [h = std::move(h), ch = std::move(ch), sh = std::move(sh)](const Stream::Config &config, base::Connection &conn) {
        auto hh = h; auto chh = ch; auto shh = sh;
        return std::shared_ptr<Stream>(new AdhocStream(conn, config, std::move(hh), std::move(chh), std::move(shh)));
      }) {}
    ~AdhocClient() override {}
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
      TcpPort(ConnectionFactory &cf) : TcpPortBase(cf.loop()), cf_(cf) {}
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
      UdpPort(ConnectionFactory &cf) : UdpPortBase(cf.loop(), cf.udp_listener_config()), cf_(cf) {
        ASSERT(&alarm_processor_ != &NopAlarmProcessor::Instance());
      }
      ConnectionFactory &connection_factory() { return cf_; }
    private:
      ConnectionFactory &cf_;
    };  
  public:
    Listener(Loop &l, Config &&config, StreamFactory &&sf) :
      ConnectionFactory(l, std::move(config), std::move(sf)),
      http_listener_(l, http_listener_config()), router_(), tcp_ports_(), udp_ports_() {}
    Listener(Loop &l, Config &&config, FactoryMethod &&fm, StreamFactory &&sf) :
      ConnectionFactory(l, std::move(config), std::move(fm), std::move(sf)),
      http_listener_(l, http_listener_config()), router_(), tcp_ports_(), udp_ports_() {}
    ~Listener() override { Fin(); }
  public:
    uint16_t udp_port() const { return udp_ports_.empty() ? 0 : udp_ports_[0].port(); }
    uint16_t tcp_port() const { return tcp_ports_.empty() ? 0 : tcp_ports_[0].port(); }
  public:
    int Accept(const std::string &client_sdp, json &response);
    void Close(BaseConnection &c) { CloseConnection(dynamic_cast<Connection &>(c)); }
    void Fin();
    bool Listen(
      int signaling_port, int port,
      const std::string &listen_ip = "", const std::string &path = "/qrpc"
    );
    HttpRouter &RestRouter() { return router_; }
    // implement ConnectionFactory
    virtual bool is_client() const override { return false; }
    virtual int Setup() override;
  protected:
    HttpListener http_listener_;
    HttpRouter router_;
    std::vector<TcpPort> tcp_ports_;
    std::vector<UdpPort> udp_ports_;
  };


  // AdhocListener
  class AdhocListener : public Listener {
  public:
    AdhocListener(Loop &l, Config &&c, Stream::Handler &&h) : Listener(l, std::move(c), 
      [h = std::move(h)](const Stream::Config &config, base::Connection &conn) {
        auto hh = h;
        return std::shared_ptr<Stream>(new AdhocStream(conn, config, std::move(hh)));
      }) {}
    AdhocListener(Loop &l, Config &&c, 
      Stream::Handler &&h, AdhocStream::ConnectHandler &&ch, AdhocStream::ShutdownHandler &&sh) :
      Listener(l, std::move(c), [h = std::move(h), ch = std::move(ch), sh = std::move(sh)](
        const Stream::Config &config, base::Connection &conn
      ) {
        auto hh = h; auto chh = ch; auto shh = sh;
        return std::shared_ptr<Stream>(new AdhocStream(conn, config, std::move(hh), std::move(chh), std::move(shh)));
      }) {}
    AdhocListener(Loop &l, Config &&c, 
      AdhocConnection::ConnectHandler &&cch, AdhocConnection::ShutdownHandler &&csh,
      Stream::Handler &&h, AdhocStream::ConnectHandler &&ch, AdhocStream::ShutdownHandler &&sh) :
      Listener(l, std::move(c), [cch = std::move(cch), csh = std::move(csh)](ConnectionFactory &cf, RTC::DtlsTransport::Role role) {
        auto cchh = cch; auto cshh = csh;
        return new AdhocConnection(cf, role, std::move(cchh), std::move(cshh));
      }, [h = std::move(h), ch = std::move(ch), sh = std::move(sh)](const Stream::Config &config, base::Connection &conn) {
        auto hh = h; auto chh = ch; auto shh = sh;
        return std::shared_ptr<Stream>(new AdhocStream(conn, config, std::move(hh), std::move(chh), std::move(shh)));
      }) {}
    ~AdhocListener() override {}
  };  
  typedef ConnectionFactory::Connection Connection;
} //namespace webrtc
} //namespace base