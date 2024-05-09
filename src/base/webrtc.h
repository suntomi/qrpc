#pragma once

#include "base/conn.h"
#include "base/http.h"
#include "base/id_factory.h"
#include "base/session.h"
#include "base/webrtc/ice.h"
#include "base/webrtc/sctp.h"
#include "base/webrtc/dtls.h"
#include "base/webrtc/candidate.h"

// TODO: if enabling srtp, this also need to be replaced with homebrew version
#include "RTC/SrtpSession.hpp"

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
    class IceUFrag : public std::string {
    public:
      IceUFrag() : std::string() {}
      IceUFrag(const std::string &s) : std::string(s) {}
      IceUFrag(const std::string &&s) : std::string(s) {}
      IceUFrag(const IceUFrag &f) : std::string(f) {}
      IceUFrag(const IceUFrag &&f) : std::string(f) {}
      IceUFrag& operator=(IceUFrag&& other) noexcept {
        if (this != &other) { dynamic_cast<std::string *>(this)->operator=(other); }
        return *this;
      }
    };
  public: // sessions
    class Connection;
    class TcpSession : public TcpListener::TcpSession {
    public:
      typedef TcpSessionFactory Factory;
      TcpSession(TcpSessionFactory &f, Fd fd, const Address &addr, std::shared_ptr<Connection> c) :
        TcpSession(f, fd, addr) { connection_ = c; }
      TcpSession(TcpSessionFactory &f, Fd fd, const Address &addr) :
        TcpSessionFactory::TcpSession(f, fd, addr), connection_() {}
      int OnRead(const char *p, size_t sz) override;
      qrpc_time_t OnShutdown() override;
    protected:
      std::shared_ptr<Connection> connection_;
    };
    class UdpSession : public UdpListener::UdpSession {
    public:
      typedef UdpSessionFactory Factory;
      UdpSession(UdpSessionFactory &f, Fd fd, const Address &addr, std::shared_ptr<Connection> c) :
        UdpSession(f, fd, addr) { connection_ = c; }
      UdpSession(UdpSessionFactory &f, Fd fd, const Address &addr) :
        UdpSessionFactory::UdpSession(f, fd, addr), connection_() {}
      int OnRead(const char *p, size_t sz) override;
      qrpc_time_t OnShutdown() override;
    protected:
      std::shared_ptr<Connection> connection_;
    };
    typedef TcpListenerOf<ConnectionFactory::TcpSession> TcpPortBase;
    class TcpPort : public TcpPortBase {
    public:
      TcpPort(ConnectionFactory &cf) : TcpPortBase(cf.loop()), cf_(cf) {}
      ConnectionFactory &connection_factory() { return cf_; }
    private:
      ConnectionFactory &cf_;
    };
    typedef UdpListenerOf<ConnectionFactory::UdpSession> UdpPortBase;
    class UdpPort : public UdpPortBase {
    public:
      UdpPort(ConnectionFactory &cf) : UdpPortBase(cf.loop(), cf.udp_listener_config()), cf_(cf) {}
      ConnectionFactory &connection_factory() { return cf_; }
    private:
      ConnectionFactory &cf_;
    };
    class SyscallStream : public AdhocStream {
    public:
      SyscallStream(BaseConnection &c, const Config &config, ConnectHandler &&h) :
        AdhocStream(c, config, std::move(Handler(Nop())), std::move(h), std::move(ShutdownHandler(Nop()))) {}
      ~SyscallStream() {}
    };
  public: // connections
    class Connection : public base::Connection, 
                       public IceServer::Listener,
                       public DtlsTransport::Listener,
                       public SctpAssociation::Listener {
    public:
      Connection(ConnectionFactory &sv, DtlsTransport::Role dtls_role) :
        sv_(sv), last_active_(qrpc_time_now()), ice_server_(nullptr), dtls_role_(dtls_role),
        dtls_transport_(nullptr), sctp_association_(nullptr),
        srtp_send_(nullptr), srtp_recv_(nullptr), streams_(), stream_id_factory_(),
        sctp_connected_(false), closed_(false) {
          // https://datatracker.ietf.org/doc/html/rfc8832#name-data_channel_open-message
          switch (dtls_role) {
            case DtlsTransport::Role::CLIENT:
              stream_id_factory_.configure(0, 2); // zero is allowed to be stream id of client
              break;
            case DtlsTransport::Role::SERVER:
              stream_id_factory_.configure(1, 2);
              break;
            default:
              DIE("invalid dtls role")
              break;          
          }
        }
      ~Connection() {}
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
      int OnConnect() override { return QRPC_OK; }
      void OnShutdown() override {}
    public:
      bool connected() const;
      inline bool closed() const { return closed_; }
      inline ConnectionFactory &factory() { return sv_; }
      inline const ConnectionFactory &factory() const { return sv_; }
      inline const IceServer &ice_server() const { return *ice_server_.get(); }
      inline DtlsTransport &dtls_transport() { return *dtls_transport_.get(); }
      // for now, qrpc server initiates dtls transport because safari does not initiate it
      // even if we specify "setup: passive" in SDP of whip response
      inline bool is_client() const { return dtls_role_ == DtlsTransport::Role::SERVER; }
    public:
      int Init(std::string &ufrag, std::string &pwd);
      void Fin();
      void Touch(qrpc_time_t now) { last_active_ = now; }
      int RunDtlsTransport();
      IceProber *InitIceProber(const std::string &ufrag, const std::string &pwd, uint64_t priority);
      void OnDtlsEstablished();
      void OnTcpSessionShutdown(Session *s);
      void OnUdpSessionShutdown(Session *s);
      std::shared_ptr<Stream> NewStream(const Stream::Config &c, const StreamFactory &sf);
      std::shared_ptr<Stream> OpenStream(const Stream::Config &c, const StreamFactory &sf);
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

      // implements DtlsTransport::Listener
			void OnDtlsTransportConnecting(const DtlsTransport* dtlsTransport) override;
			void OnDtlsTransportConnected(
			  const DtlsTransport* dtlsTransport,
			  RTC::SrtpSession::CryptoSuite srtpCryptoSuite,
			  uint8_t* srtpLocalKey,
			  size_t srtpLocalKeyLen,
			  uint8_t* srtpRemoteKey,
			  size_t srtpRemoteKeyLen,
			  std::string& remoteCert) override;
			// The DTLS connection has been closed as the result of an error (such as a
			// DTLS alert or a failure to validate the remote fingerprint).
			void OnDtlsTransportFailed(const DtlsTransport* dtlsTransport) override;
			// The DTLS connection has been closed due to receipt of a close_notify alert.
			void OnDtlsTransportClosed(const DtlsTransport* dtlsTransport) override;
			// Need to send DTLS data to the peer.
			void OnDtlsTransportSendData(
			  const DtlsTransport* dtlsTransport, const uint8_t* data, size_t len) override;
			// DTLS application data received.
			void OnDtlsTransportApplicationDataReceived(
			  const DtlsTransport* dtlsTransport, const uint8_t* data, size_t len) override;  

      // implements SctpAssociation::Listener
			void OnSctpAssociationConnecting(SctpAssociation* sctpAssociation) override;
			void OnSctpAssociationConnected(SctpAssociation* sctpAssociation)  override;
			void OnSctpAssociationFailed(SctpAssociation* sctpAssociation)     override;
			void OnSctpAssociationClosed(SctpAssociation* sctpAssociation)     override;
			void OnSctpAssociationSendData(
			  SctpAssociation* sctpAssociation, const uint8_t* data, size_t len) override;
			void OnSctpStreamReset(
			  SctpAssociation* sctpAssociation, uint16_t streamId) override;        
			void OnSctpWebRtcDataChannelControlDataReceived(
			  SctpAssociation* sctpAssociation,
			  uint16_t streamId,
			  const uint8_t* msg,
			  size_t len) override;
			void OnSctpAssociationMessageReceived(
			  SctpAssociation* sctpAssociation,
			  uint16_t streamId,
			  uint32_t ppid,
			  const uint8_t* msg,
			  size_t len) override;
			void OnSctpAssociationBufferedAmount(
			  SctpAssociation* sctpAssociation, uint32_t len) override;    
    protected:
      qrpc_time_t last_active_;
      ConnectionFactory &sv_;
      std::unique_ptr<IceServer> ice_server_; // ICE
      std::unique_ptr<IceProber> ice_prober_; // ICE(client)
      DtlsTransport::Role dtls_role_;
      std::unique_ptr<DtlsTransport> dtls_transport_; // DTLS
      std::unique_ptr<SctpAssociation> sctp_association_; // SCTP
      std::unique_ptr<RTC::SrtpSession> srtp_send_, srtp_recv_; // SRTP
      std::map<Stream::Id, std::shared_ptr<Stream>> streams_;
      IdFactory<Stream::Id> stream_id_factory_;
      bool sctp_connected_, closed_;
    };
    typedef std::function<Connection *(ConnectionFactory &, DtlsTransport::Role)> FactoryMethod;
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
      std::string ip{""};
      std::vector<Port> ports;
      size_t max_outgoing_stream_size, initial_incoming_stream_size;
      size_t send_buffer_size;
      qrpc_time_t udp_session_timeout;
      qrpc_time_t connection_timeout;
      AlarmProcessor &alarm_processor;
      std::string fingerprint_algorithm;
      bool in6{false};

      // derived from above config values
      std::string fingerprint{""};
      std::vector<std::string> ifaddrs;
    public:
      int Derive(AlarmProcessor &ap);
    };
  public:
    ConnectionFactory(Loop &l, Config &&config, FactoryMethod &&fm, StreamFactory &&sf) :
      loop_(l), config_(config), factory_method_(fm), stream_factory_(sf), connections_() {}
    ConnectionFactory(Loop &l, Config &&config, StreamFactory &&sf) :
      loop_(l), config_(config), factory_method_([this](ConnectionFactory &cf, DtlsTransport::Role role) {
        return new Connection(cf, role);
      }), stream_factory_(sf), connections_() {}
    ~ConnectionFactory() { Fin(); }
  public:
    Loop &loop() { return loop_; }
    const Config &config() const { return config_; }
    StreamFactory &stream_factory() { return stream_factory_; }
    AlarmProcessor &alarm_processor() { return config_.alarm_processor; }
    template <class F> inline F& to() { return reinterpret_cast<F &>(*this); }
    template <class F> inline const F& to() const { return reinterpret_cast<const F &>(*this); }
    const std::string &fingerprint() const { return config_.fingerprint; }
    const std::string &fingerprint_algorithm() const { return config_.fingerprint_algorithm; }
    const UdpSessionFactory::Config udp_listener_config() const {
      return { .alarm_processor = config_.alarm_processor, .session_timeout = config_.udp_session_timeout };
    }
    uint16_t udp_port() const { return udp_ports_.empty() ? 0 : udp_ports_[0].port(); }
    uint16_t tcp_port() const { return tcp_ports_.empty() ? 0 : tcp_ports_[0].port(); }
    const std::string primary_proto() const {
      return config_.ports[0].protocol == Port::Protocol::UDP ? "UDP" : "TCP";
    }
  public:
    int Init();
    void Fin();
    std::shared_ptr<Connection> FindFromUfrag(const IceUFrag &ufrag);
    std::shared_ptr<Connection> FindFromStunRequest(const uint8_t *p, size_t sz);
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
                // inside Close, the entry will be erased
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
    std::vector<TcpPort> tcp_ports_;
    std::vector<UdpPort> udp_ports_;
    std::map<IceUFrag, std::shared_ptr<Connection>> connections_;
  private:
    static uint32_t g_ref_count_;
    static std::mutex g_ref_sync_mutex_;
    static int GlobalInit(AlarmProcessor &a);
    static void GlobalFin();
  };
  class AdhocConnection : public ConnectionFactory::Connection {
  public:
    typedef std::function<int (ConnectionFactory::Connection &)> ConnectHandler;
    typedef std::function<void (ConnectionFactory::Connection &)> ShutdownHandler;
  public:
    AdhocConnection(ConnectionFactory &sv, DtlsTransport::Role dtls_role, ConnectHandler &&ch, ShutdownHandler &&sh) :
      Connection(sv, dtls_role), connect_handler_(std::move(ch)), shutdown_handler_(std::move(sh)) {};
    int OnConnect() override { return connect_handler_(*this); }
    void OnShutdown() override { shutdown_handler_(*this); }
  private:
    ConnectHandler connect_handler_;
    ShutdownHandler shutdown_handler_;
  };


  // Client
  class Client : public ConnectionFactory, public base::Client {
  public:
    Client(Loop &l, Config &&config, StreamFactory &&sf) :
      ConnectionFactory(l, std::move(config), std::move(sf)), http_client_(l, config.alarm_processor) {}
    Client(Loop &l, Config &&config, FactoryMethod &&fm, StreamFactory &&sf) :
      ConnectionFactory(l, std::move(config), std::move(fm), std::move(sf)), http_client_(l, config.alarm_processor) {}
    ~Client() { Fin(); }
  public:
    // implement base::Client
    bool Connect(const std::string &host, int port, const std::string &path) override;
    void Close(BaseConnection &c) override { CloseConnection(dynamic_cast<Connection &>(c)); }
  public:
    int Offer(std::string &sdp, std::string &ufrag);
    bool Open(const std::vector<Candidate> &candidate, size_t idx, std::shared_ptr<Connection> &c);
  protected:
    HttpClient http_client_;
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
      Client(l, std::move(c), [cch = std::move(cch), csh = std::move(csh)](ConnectionFactory &cf, DtlsTransport::Role role) {
        auto cchh = cch; auto cshh = csh;
        return new AdhocConnection(cf, role, std::move(cchh), std::move(cshh));
      }, [h = std::move(h), ch = std::move(ch), sh = std::move(sh)](const Stream::Config &config, base::Connection &conn) {
        auto hh = h; auto chh = ch; auto shh = sh;
        return std::shared_ptr<Stream>(new AdhocStream(conn, config, std::move(hh), std::move(chh), std::move(shh)));
      }) {}
    ~AdhocClient() {}
  };  


  // Server
  class Server : public ConnectionFactory, public base::Server {
  public:
    Server(Loop &l, Config &&config, StreamFactory &&sf) :
      ConnectionFactory(l, std::move(config), std::move(sf)), http_listener_(l), router_() {}
    Server(Loop &l, Config &&config, FactoryMethod &&fm, StreamFactory &&sf) :
      ConnectionFactory(l, std::move(config), std::move(fm), std::move(sf)), http_listener_(l), router_() {}
    ~Server() { Fin(); }
  public:
    int Accept(const std::string &client_sdp, std::string &server_sdp);
    // implements base::Server
    void Close(BaseConnection &c) override { CloseConnection(dynamic_cast<Connection &>(c)); }
    bool Listen(
      int signaling_port, int port,
      const std::string &listen_ip, const std::string &path
    ) override;
    HttpRouter &RestRouter() override { return router_; }
  protected:
    HttpListener http_listener_;
    HttpRouter router_;
  };


  // AdhocServer
  class AdhocServer : public Server {
  public:
    AdhocServer(Loop &l, Config &&c, Stream::Handler &&h) : Server(l, std::move(c), 
      [h = std::move(h)](const Stream::Config &config, base::Connection &conn) {
        auto hh = h;
        return std::shared_ptr<Stream>(new AdhocStream(conn, config, std::move(hh)));
      }) {}
    AdhocServer(Loop &l, Config &&c, 
      Stream::Handler &&h, AdhocStream::ConnectHandler &&ch, AdhocStream::ShutdownHandler &&sh) :
      Server(l, std::move(c), [h = std::move(h), ch = std::move(ch), sh = std::move(sh)](
        const Stream::Config &config, base::Connection &conn
      ) {
        auto hh = h; auto chh = ch; auto shh = sh;
        return std::shared_ptr<Stream>(new AdhocStream(conn, config, std::move(hh), std::move(chh), std::move(shh)));
      }) {}
    AdhocServer(Loop &l, Config &&c, 
      AdhocConnection::ConnectHandler &&cch, AdhocConnection::ShutdownHandler &&csh,
      Stream::Handler &&h, AdhocStream::ConnectHandler &&ch, AdhocStream::ShutdownHandler &&sh) :
      Server(l, std::move(c), [cch = std::move(cch), csh = std::move(csh)](ConnectionFactory &cf, DtlsTransport::Role role) {
        auto cchh = cch; auto cshh = csh;
        return new AdhocConnection(cf, role, std::move(cchh), std::move(cshh));
      }, [h = std::move(h), ch = std::move(ch), sh = std::move(sh)](const Stream::Config &config, base::Connection &conn) {
        auto hh = h; auto chh = ch; auto shh = sh;
        return std::shared_ptr<Stream>(new AdhocStream(conn, config, std::move(hh), std::move(chh), std::move(shh)));
      }) {}
    ~AdhocServer() {}
  };  
  typedef ConnectionFactory::Connection Connection;
} //namespace webrtc
} //namespace base