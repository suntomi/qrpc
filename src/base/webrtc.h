#pragma once

#include "base/id_factory.h"
#include "base/session.h"
#include "base/stream.h"
#include "base/webrtc/ice.h"
#include "base/webrtc/sctp.h"
#include "base/webrtc/dtls.h"

// TODO: if enabling srtp, this also need to be replaced with homebrew version
#include "RTC/SrtpSession.hpp"

namespace base {
  class WebRTCServer {
  public:
    class IceUFlag : public std::string {
    public:
      IceUFlag(const std::string &s) : std::string(s) {}
      IceUFlag(const std::string &&s) : std::string(s) {}
      IceUFlag(const IceUFlag &f) : std::string(f) {}
      IceUFlag(const IceUFlag &&f) : std::string(f) {}
    };
  public: // sessions
    class Connection;
    class TcpSession : public TcpListener::TcpSession {
    public:
      TcpSession(TcpListener &f, Fd fd, const Address &addr) : 
        TcpListener::TcpSession(f, fd, addr), connection_() {}
      int OnRead(const char *p, size_t sz) override;
    private:
      std::shared_ptr<Connection> connection_;
    };
    class UdpSession : public UdpListener::UdpSession {
    public:
      UdpSession(UdpListener &f, Fd fd, const Address &addr) : 
        UdpListener::UdpSession(f, fd, addr), connection_() {}
      int OnRead(const char *p, size_t sz) override;
    private:
      std::shared_ptr<Connection> connection_;
    };
  public: // servers
    typedef TcpServer<TcpSession> BaseTcpServer;
    typedef UdpServer<UdpSession> BaseUdpServer;
    class TcpPort : public BaseTcpServer {
    public:
      TcpPort(WebRTCServer &ws) : BaseTcpServer(ws.loop()), webrtc_server_(ws) {}
      WebRTCServer &webrtc_server() { return webrtc_server_; }
    private:
      WebRTCServer &webrtc_server_;
    };
    class UdpPort : public BaseUdpServer {
    public:
      UdpPort(WebRTCServer &ws) : BaseUdpServer(ws.loop(), ws.udp_listener_config()), webrtc_server_(ws) {}
      WebRTCServer &webrtc_server() { return webrtc_server_; }
    private:
      WebRTCServer &webrtc_server_;
    };
  public: // connections
    class Connection : public IceServer::Listener,
                       public DtlsTransport::Listener,
                       public SctpAssociation::Listener,
                       public Stream::Processor {
    public:
      Connection(WebRTCServer &sv, DtlsTransport::Role dtls_role) :
        sv_(sv), last_active_(0), ice_server_(nullptr), dtls_role_(dtls_role),
        dtls_transport_(nullptr), sctp_association_(nullptr),
        srtp_send_(nullptr), srtp_recv_(nullptr), sctp_connected_(false),
        streams_(), stream_id_factory_() {
          stream_id_factory_.set_incr(2);
          // https://datatracker.ietf.org/doc/html/rfc8832#name-data_channel_open-message
          switch (dtls_role) {
            case DtlsTransport::Role::CLIENT:
              stream_id_factory_.set_init(2);
              break;
            case DtlsTransport::Role::SERVER:
              stream_id_factory_.set_init(1);
              break;
            default:
              DIE("invalid dtls role")
              break;          
          }
        }
      ~Connection() {}
    public:
      bool connected() const;
      WebRTCServer &server() { return sv_; }
      const WebRTCServer &server() const { return sv_; }
      const IceServer *ice_server() const { return ice_server_.get(); }
    public:
      int Init(std::string &uflag, std::string &pwd);
      int RunDtlsTransport();
      std::shared_ptr<Stream> OpenStream(Stream::Config &c);
      std::shared_ptr<Stream> NewStream(Stream::Config &c);
    public:
      // entry point of all incoming packets
      int OnPacketReceived(Session *session, const uint8_t *p, size_t sz);
      // protocol handlers
      int OnStunDataReceived(Session *session, const uint8_t *p, size_t sz);
      int OnDtlsDataReceived(Session *session, const uint8_t *p, size_t sz);
      int OnRtcpDataReceived(Session *session, const uint8_t *p, size_t sz);
      int OnRtpDataReceived(Session *session, const uint8_t *p, size_t sz);      
    public:
      // implements Stream::Processor
      int Send(Stream &s, const char *p, size_t sz, bool binary) override;
      void Close(Stream &s) override;
      int Open(Stream &s) override;

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
      WebRTCServer &sv_;
      std::unique_ptr<IceServer> ice_server_; // ICE
      DtlsTransport::Role dtls_role_;
      std::unique_ptr<DtlsTransport> dtls_transport_; // DTLS
      std::unique_ptr<SctpAssociation> sctp_association_; // SCTP
      std::unique_ptr<RTC::SrtpSession> srtp_send_, srtp_recv_; // SRTP
      bool sctp_connected_;
      std::map<Stream::Id, std::shared_ptr<Stream>> streams_;
      IdFactory<Stream::Id> stream_id_factory_;
    };
    struct Port {
      enum Protocol {
        NONE = 0,
        UDP = 1,
        TCP = 2,
      };
      Protocol protocol;
      std::string ip;
      int port;
      uint32_t priority;
    };
    struct Config {
      std::vector<Port> ports;
      std::string ca, cert, key;
      size_t max_outgoing_stream_size, initial_incoming_stream_size;
      size_t sctp_send_buffer_size;
      qrpc_time_t udp_session_timeout;
      AlarmProcessor &alarm_processor;

      // derived from above config values
      std::string fingerprint;
    public:
      int Derive(AlarmProcessor &ap);
    };
  public:
    typedef std::function<std::shared_ptr<Stream> (const Stream::Config &, WebRTCServer::Connection &)> StreamFactory;
  public:
    WebRTCServer(Loop &l, Config &&config, const StreamFactory &sf) :
      loop_(l), config_(config), stream_factory_(sf),
      tcp_ports_(), udp_ports_(), connections_() {}
    ~WebRTCServer() {}
  public:
    Loop &loop() { return loop_; }
    const Config &config() const { return config_; }
    StreamFactory &stream_factory() { return stream_factory_; }
    AlarmProcessor &alarm_processor() { return config_.alarm_processor; }
    uint16_t udp_port() const { return udp_ports_.empty() ? 0 : udp_ports_[0].port(); }
    uint16_t tcp_port() const { return tcp_ports_.empty() ? 0 : tcp_ports_[0].port(); }
    const std::string &fingerprint() const { return config_.fingerprint; }
    const UdpPort::Config udp_listener_config() const {
      return { .alarm_processor = config_.alarm_processor, .session_timeout = config_.udp_session_timeout };
    }
  public:
    int Init();
    void Fin();
    int NewConnection(const std::string &client_sdp, std::string &server_sdp);
    std::shared_ptr<Connection> FindFromStunRequest(const uint8_t *p, size_t sz);
    void RemoveUFlag(IceUFlag &uflag) {
      // raw pointer in map might be freed
      if (connections_.erase(uflag) == 0) {
        logger::warn({{"ev","fail to remove uflag"},{"uflag",uflag}});
      }
    }
  protected:
    Loop &loop_;
    Config config_;
    StreamFactory stream_factory_;
    std::vector<TcpPort> tcp_ports_;
    std::vector<UdpPort> udp_ports_;
    std::map<IceUFlag, std::shared_ptr<Connection>> connections_;
  private:
    static int GlobalInit(AlarmProcessor &a);
    static void GlobalFin();
  };
  class AdhocWebRTCServer : public WebRTCServer {
  public:
    AdhocWebRTCServer(Loop &l, Config &&c, const Stream::Handler &h) : WebRTCServer(l, std::move(c), 
      [&h](const Stream::Config &config, WebRTCServer::Connection &conn) {
        return std::shared_ptr<Stream>(new AdhocStream(conn, config, h));
      }) {}
    ~AdhocWebRTCServer() {}
  };
  typedef WebRTCServer::Connection Connection;
} //namespace base