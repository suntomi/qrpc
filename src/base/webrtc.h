#pragma once

#include "base/session.h"
#include "base/webrtc/ice.h"

#include "RTC/IceCandidate.hpp"
#include "RTC/DtlsTransport.hpp"
#include "RTC/SctpAssociation.hpp"

namespace base {
  class WebRTCServer {
  public:
    class IceUserName : public std::string {};
  public: // sessions
    class TcpSession : public TcpListener::TcpSession {
    public:
      TcpSession(SessionFactory &f, Fd fd, const Address &addr) : 
        TcpListener::TcpSession(f, fd, addr) {}
        int OnRead(const char *p, size_t sz) override {
          return QRPC_OK;
        }
    };
    class UdpSession : public UdpListener::UdpSession {
    public:
      UdpSession(SessionFactory &f, Fd fd, const Address &addr) : 
        UdpListener::UdpSession(f, fd, addr) {}
      int OnRead(const char *p, size_t sz) override {
          return QRPC_OK;        
      }
    };
  public: // servers
    typedef TcpServer<TcpSession> BaseTcpServer;
    typedef UdpServer<UdpSession> BaseUdpServer;
    class TcpPort : public BaseTcpServer {
    public:
        TcpPort(Loop &l) : BaseTcpServer(l) {}
    };
    class UdpPort : public BaseUdpServer {
    public:
        UdpPort(Loop &l) : BaseUdpServer(l) {}
    };
  public: // connections
    class Connection : public IceServer::Listener,
                       public RTC::DtlsTransport::Listener,
                       public RTC::SctpAssociation::Listener {
    public:
      Connection(WebRTCServer &sv) : sv_(sv), last_active_(0),
        ice_server_(nullptr), dtls_transport_(nullptr), sctp_association_(nullptr) {}
      ~Connection() {}
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

      // implements RTC::SctpAssociation::Listener
			void OnSctpAssociationConnecting(RTC::SctpAssociation* sctpAssociation) override;
			void OnSctpAssociationConnected(RTC::SctpAssociation* sctpAssociation)  override;
			void OnSctpAssociationFailed(RTC::SctpAssociation* sctpAssociation)     override;
			void OnSctpAssociationClosed(RTC::SctpAssociation* sctpAssociation)     override;
			void OnSctpAssociationSendData(
			  RTC::SctpAssociation* sctpAssociation, const uint8_t* data, size_t len) override;
			void OnSctpAssociationMessageReceived(
			  RTC::SctpAssociation* sctpAssociation,
			  uint16_t streamId,
			  uint32_t ppid,
			  const uint8_t* msg,
			  size_t len) override;
			void OnSctpAssociationBufferedAmount(
			  RTC::SctpAssociation* sctpAssociation, uint32_t len) override;    
    protected:
      qrpc_time_t last_active_;
      WebRTCServer &sv_;
      IceServer *ice_server_; // ICE
      RTC::DtlsTransport *dtls_transport_; // DTLS
      RTC::SctpAssociation *sctp_association_; // SCTP
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
    int Init();
    void Fin();
    int NewConnection(const std::string &client_sdp, std::string &server_sdp);
  protected:
    Loop &loop_;
    std::vector<Config> configs_;
    AlarmProcessor *alarm_processor_;
    std::vector<TcpPort> tcp_ports_;
    std::vector<UdpPort> udp_ports_;
    std::map<IceUserName, Connection> connections_;
  private:
    static int GlobalInit();
    static void GlobalFin();
  };
} //namespace base