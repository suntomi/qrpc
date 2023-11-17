#pragma once

#include "base/id_factory.h"
#include "base/session.h"
#include "base/stream.h"
#include "base/webrtc/ice.h"
#include "base/webrtc/sctp.h"

#include "RTC/IceCandidate.hpp"
#include "RTC/DtlsTransport.hpp"
#include "RTC/SrtpSession.hpp"

namespace base {
  class WebRTCServer {
  public:
    class IceUFlag : public std::string {};
  public: // sessions
    class Connection;
    class TcpSession : public TcpListener::TcpSession {
    public:
      TcpSession(SessionFactory &f, Fd fd, const Address &addr) : 
        TcpListener::TcpSession(f, fd, addr), connection_(nullptr) {}
        int OnRead(const char *p, size_t sz) override;
    private:
      Connection *connection_;
    };
    class UdpSession : public UdpListener::UdpSession {
    public:
      UdpSession(SessionFactory &f, Fd fd, const Address &addr) : 
        UdpListener::UdpSession(f, fd, addr), connection_(nullptr) {}
      int OnRead(const char *p, size_t sz) override;
    private:
      Connection *connection_;
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
      UdpPort(WebRTCServer &ws) : BaseUdpServer(ws.loop()), webrtc_server_(ws) {}
      WebRTCServer &webrtc_server() { return webrtc_server_; }
    private:
      WebRTCServer &webrtc_server_;
    };
  public: // connections
    class Connection : public IceServer::Listener,
                       public RTC::DtlsTransport::Listener,
                       public SctpAssociation::Listener,
                       public Stream::Processor {
    public:
      Connection(WebRTCServer &sv, RTC::DtlsTransport::Role dtls_role) :
        sv_(sv), last_active_(0), ice_server_(nullptr), dtls_role_(dtls_role),
        dtls_transport_(nullptr), sctp_association_(nullptr),
        srtp_send_(nullptr), srtp_recv_(nullptr), sctp_connected_(false) {}
      ~Connection() {}
    public:
      bool connected() const;
    public:
      int RunDtlsTransport();
      Stream *NewStream(Stream::Config &c, Stream::Handler &h);
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
      int Send(Stream *s, const char *p, size_t sz, bool binary) override;
      void Close(Stream *s) override;

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
			void OnSctpAssociationConnecting(SctpAssociation* sctpAssociation) override;
			void OnSctpAssociationConnected(SctpAssociation* sctpAssociation)  override;
			void OnSctpAssociationFailed(SctpAssociation* sctpAssociation)     override;
			void OnSctpAssociationClosed(SctpAssociation* sctpAssociation)     override;
			void OnSctpAssociationSendData(
			  SctpAssociation* sctpAssociation, const uint8_t* data, size_t len) override;
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
      IceServer *ice_server_; // ICE
      RTC::DtlsTransport::Role dtls_role_;
      RTC::DtlsTransport *dtls_transport_; // DTLS
      SctpAssociation *sctp_association_; // SCTP
      RTC::SrtpSession *srtp_send_, *srtp_recv_; // SRTP
      bool sctp_connected_;
      std::map<Stream::Id, Stream> streams_;
      IdFactory<Stream::Id> stream_id_factory_;
    };
    enum PPID {
      STRING = 51,
      BINARY_PARTIAL = 52, //deprecated
      BINARY = 53,
      STRING_PARTIAL = 54, //deprecated
      STRING_EMPTY = 56,
      BINARY_EMPTY = 57,
    };
    struct Config {
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
  public:
    WebRTCServer(
      Loop &l, AlarmProcessor *alarm_processor, std::vector<Config> configs
    ) : loop_(l), configs_(configs), alarm_processor_(alarm_processor),
        tcp_ports_(), udp_ports_(), connections_() {}
    ~WebRTCServer() {}
  public:
    Loop &loop() { return loop_; }
  public:
    int Init();
    void Fin();
    int NewConnection(const std::string &client_sdp, std::string &server_sdp);
    Connection *FindFromStunRequest(const uint8_t *p, size_t sz);
    void RemoveUFlag(IceUFlag &uflag) {
      if (connections_.erase(uflag) == 0) {
        logger::warn({{"ev","fail to remove uflag"},{"uflag",uflag}});
      }
    }
  protected:
    Loop &loop_;
    std::vector<Config> configs_;
    AlarmProcessor *alarm_processor_;
    std::vector<TcpPort> tcp_ports_;
    std::vector<UdpPort> udp_ports_;
    std::map<IceUFlag, Connection> connections_;
  private:
    static int GlobalInit(AlarmProcessor *a);
    static void GlobalFin();
  };
  typedef WebRTCServer::Connection Connection;
} //namespace base