#include "base/defs.h"
#include "base/webrtc.h"
#include "base/webrtc/sctp.h"

#include "common.hpp"
#include "MediaSoupErrors.hpp"
#include "DepLibSRTP.hpp"
#include "DepLibUV.hpp"
#include "DepLibWebRTC.hpp"
#include "DepOpenSSL.hpp"
#include "RTC/DtlsTransport.hpp"
#include "RTC/SrtpSession.hpp"
#include "RTC/StunPacket.hpp"
#include "RTC/RtpPacket.hpp"
#include "RTC/RTCP/Packet.hpp"

#include <algorithm>

namespace base {

// WebRTCServer
int WebRTCServer::Init() {
  int r;
  if ((r = GlobalInit(alarm_processor_)) < 0) {
    return r;
  }
  return QRPC_OK;
}
void WebRTCServer::Fin() {
  GlobalFin();
}
int WebRTCServer::NewConnection(const std::string &client_sdp, std::string &server_sdp) {
  return QRPC_OK;
}
static inline WebRTCServer::IceUFlag GetLocalIceUFragFrom(RTC::StunPacket* packet) {
		TRACK();

		// Here we inspect the USERNAME attribute of a received STUN request and
		// extract its remote usernameFragment (the one given to our IceServer as
		// local usernameFragment) which is the first value in the attribute value
		// before the ":" symbol.

		const auto& username  = packet->GetUsername();
		const size_t colonPos = username.find(':');

		// If no colon is found just return the whole USERNAME attribute anyway.
		if (colonPos == std::string::npos) {
			return WebRTCServer::IceUFlag{username};
    }

		return WebRTCServer::IceUFlag{username.substr(0, colonPos)};
}
WebRTCServer::Connection *WebRTCServer::FindFromStunRequest(const uint8_t *p, size_t sz) {
  RTC::StunPacket* packet = RTC::StunPacket::Parse(p, sz);
  if (packet == nullptr) {
    QRPC_LOG(warn, "ignoring wrong STUN packet received");
    return nullptr;
  }
  // try to match the local ICE username fragment.
  auto key = GetLocalIceUFragFrom(packet);
  auto it = connections_.find(key);
  delete packet;

  if (it == this->connections_.end()) {
    logger::error({
      {"ev","ignoring received STUN packet with unknown remote ICE usernameFragment"},
      {"uflag",key}
    });
    return nullptr;
  }
  return &it->second;
}

int WebRTCServer::GlobalInit(AlarmProcessor *a) {
	try
	{
		// Initialize static stuff.
		DepOpenSSL::ClassInit();
		DepLibSRTP::ClassInit();
		DepUsrSCTP::ClassInit(*a);
		DepLibWebRTC::ClassInit();
		Utils::Crypto::ClassInit();
		RTC::DtlsTransport::ClassInit();
		RTC::SrtpSession::ClassInit();

		return QRPC_OK;
	} catch (const MediaSoupError& error) {
		logger::die({{"ev","mediasoup setup failure"},{"reason",error.what()}});
    // no return
		return QRPC_EDEPS;
	}
}
void WebRTCServer::GlobalFin() {
	try
	{
    // Free static stuff.
		DepLibSRTP::ClassDestroy();
		Utils::Crypto::ClassDestroy();
		DepLibWebRTC::ClassDestroy();
		RTC::DtlsTransport::ClassDestroy();
		DepUsrSCTP::ClassDestroy();
		DepLibUV::ClassDestroy();
  }
  catch (const MediaSoupError& error) {
    logger::error({{"ev","mediasoup cleanup failure"},{"reason",error.what()}});
	}
}

// WebRTCServer::UdpSession/TcpSession
int WebRTCServer::TcpSession::OnRead(const char *p, size_t sz) {
  auto up = reinterpret_cast<const uint8_t *>(p);
  if (connection_ == nullptr) {
    connection_ = factory().to<TcpPort>().webrtc_server().FindFromStunRequest(up, sz);
    if (connection_ == nullptr) {
      ASSERT(false);
      return QRPC_EINVAL;
    }
  }
  return connection_->OnPacketReceived(this, up, sz);
}
int WebRTCServer::UdpSession::OnRead(const char *p, size_t sz) {
  auto up = reinterpret_cast<const uint8_t *>(p);
  if (connection_ == nullptr) {
    connection_ = factory().to<UdpPort>().webrtc_server().FindFromStunRequest(up, sz);
    if (connection_ == nullptr) {
      ASSERT(false);
      return QRPC_EINVAL;
    }
  }
  return connection_->OnPacketReceived(this, up, sz);
}


/* WebRTCServer::Connection */
bool WebRTCServer::Connection::connected() const {
  return (
    (
      ice_server_->GetState() == IceServer::IceState::CONNECTED ||
      ice_server_->GetState() == IceServer::IceState::COMPLETED
    ) && dtls_transport_->GetState() == RTC::DtlsTransport::DtlsState::CONNECTED
  );
}
Stream *WebRTCServer::Connection::NewStream(Stream::Config &c, Stream::Handler &h) {
  if (c.streamId == 0) {
    size_t cnt = 0;
    do {
      // auto allocate
      c.streamId = stream_id_factory_.New();
    } while (streams_.find(c.streamId) != streams_.end() && ++cnt <= 0xFFFF);
    if (cnt > 0xFFFF) {
      ASSERT(false);
      logger::error({{"ev","cannot allocate stream id"}});
      return nullptr;
    }
  } else if (streams_.find(c.streamId) != streams_.end()) {
    ASSERT(false);
    logger::error({{"ev","stream id already used"},{"sid",c.streamId}});
    return nullptr;
  }
  Stream *s = &streams_.emplace(std::piecewise_construct,
    std::forward_as_tuple(c.streamId),
    std::forward_as_tuple(this, c, h)
  ).first->second;
  ASSERT(s != nullptr);
  int r;
  if ((r = s->OnConnect()) < 0) {
    logger::info({{"ev","new stream creation blocked"},{"sid",c.streamId},{"rc",r}});
    return nullptr;    
  }
  // allocate stream Id
  sctp_association_->HandleDataConsumer(s);
  logger::info({{"ev","new stream created"},{"sid",c.streamId}});
  return s;
}
int WebRTCServer::Connection::RunDtlsTransport() {
  TRACK();

  // Do nothing if we have the same local DTLS role as the DTLS transport.
  // NOTE: local role in DTLS transport can be NONE, but not ours.
  if (dtls_transport_->GetLocalRole() == dtls_role_) {
    return QRPC_OK;
  }
  // Check our local DTLS role.
  switch (dtls_role_) {
    // If still 'auto' then transition to 'server' if ICE is 'connected' or
    // 'completed'.
    case RTC::DtlsTransport::Role::AUTO: {
      if (
        ice_server_->GetState() == IceServer::IceState::CONNECTED ||
        ice_server_->GetState() == IceServer::IceState::COMPLETED
      ) {
        logger::info(
          {{"proto","dtls"},{"ev","transition from DTLS local role 'auto' to 'server' and running DTLS transport"}}
        );
        dtls_role_ = RTC::DtlsTransport::Role::SERVER;
        dtls_transport_->Run(RTC::DtlsTransport::Role::SERVER);
      }
      break;
    }
    // 'client' is only set if a 'connect' request was previously called with
    // remote DTLS role 'server'.
    //
    // If 'client' then wait for ICE to be 'completed' (got USE-CANDIDATE).
    //
    // NOTE: This is the theory, however let's be more flexible as told here:
    //   https://bugs.chromium.org/p/webrtc/issues/detail?id=3661
    case RTC::DtlsTransport::Role::CLIENT: {
      if (
        ice_server_->GetState() == IceServer::IceState::CONNECTED ||
        ice_server_->GetState() == IceServer::IceState::COMPLETED
      ) {
        logger::debug({{"proto","dtls"},{"ev","running DTLS transport in local role 'client'"}});
        dtls_transport_->Run(RTC::DtlsTransport::Role::CLIENT);
      }
      break;
    }

    // If 'server' then run the DTLS transport if ICE is 'connected' (not yet
    // USE-CANDIDATE) or 'completed'.
    case RTC::DtlsTransport::Role::SERVER: {
      if (
        ice_server_->GetState() == IceServer::IceState::CONNECTED ||
        ice_server_->GetState() == IceServer::IceState::COMPLETED
      ) {
        logger::debug({{"proto","dtls"},{"ev","running DTLS transport in local role 'server'"}});
        dtls_transport_->Run(RTC::DtlsTransport::Role::SERVER);
      }
      break;
    }

    default: logger::die({{"ev","invalid local DTLS role"},{"role",dtls_role_}});
  }
  return QRPC_OK;
}
int WebRTCServer::Connection::OnPacketReceived(Session *session, const uint8_t *p, size_t sz) {
  // Check if it's STUN.
  if (RTC::StunPacket::IsStun(p, sz)) {
    return OnStunDataReceived(session, p, sz);
  } else if (RTC::DtlsTransport::IsDtls(p, sz)) { // Check if it's DTLS.
    return OnDtlsDataReceived(session, p, sz);
  } else if (RTC::RTCP::Packet::IsRtcp(p, sz)) { // Check if it's RTCP.
    return OnRtcpDataReceived(session, p, sz);
  } else if (RTC::RtpPacket::IsRtp(p, sz)) { // Check if it's RTP.
    return OnRtpDataReceived(session, p, sz);
  } else {
    logger::warn({
      {"ev","ignoring received packet of unknown type"},
      {"payload",str::HexDump(p, std::min((size_t)16, sz))}
    });
    return QRPC_OK;
  }
}
int WebRTCServer::Connection::OnStunDataReceived(Session *session, const uint8_t *p, size_t sz) {
  RTC::StunPacket* packet = RTC::StunPacket::Parse(p, sz);
  if (packet == nullptr) {
    logger::warn({{"ev","ignoring wrong STUN packet received"},{"proto","stun"}});
    return QRPC_OK;
  }
  ice_server_->ProcessStunPacket(packet, session);
  delete packet;
  return QRPC_OK;
}
int WebRTCServer::Connection::OnDtlsDataReceived(Session *session, const uint8_t *p, size_t sz) {
  TRACK();
  // Ensure it comes from a valid tuple.
  if (!ice_server_->IsValidTuple(session)) {
    logger::warn({{"ev","ignoring DTLS data coming from an invalid session"},{"proto","dtls"}});
    return QRPC_OK;
  }
  // Trick for clients performing aggressive ICE regardless we are ICE-Lite.
  ice_server_->MayForceSelectedSession(session);
  // Check that DTLS status is 'connecting' or 'connected'.
  if (
    dtls_transport_->GetState() == RTC::DtlsTransport::DtlsState::CONNECTING ||
    dtls_transport_->GetState() == RTC::DtlsTransport::DtlsState::CONNECTED) {
    logger::debug({{"ev","DTLS data received, passing it to the DTLS transport"},{"proto","dtls"}});
    dtls_transport_->ProcessDtlsData(p, sz);
  } else {
    logger::warn({
      {"ev","ignoring received DTLS data by invalid state"},{"proto","dtls"},
      {"state",dtls_transport_->GetState()}
    });
    return QRPC_OK;
  }  
  return QRPC_OK;
}
int WebRTCServer::Connection::OnRtcpDataReceived(Session *session, const uint8_t *p, size_t sz) {
  TRACK();
  // Ensure DTLS is connected.
  if (dtls_transport_->GetState() != RTC::DtlsTransport::DtlsState::CONNECTED) {
    logger::debug({{"ev","ignoring RTCP packet while DTLS not connected"},{"proto","dtls,rtcp"}});
    return QRPC_OK;
  }
  // Ensure there is receiving SRTP session.
  if (srtp_recv_ != nullptr) {
    logger::debug({{"proto","srtp"},{"ev","ignoring RTCP packet due to non receiving SRTP session"}});
    return QRPC_OK;
  }
  // Ensure it comes from a valid tuple.
  if (!ice_server_->IsValidTuple(session)) {
    logger::warn({{"proto","rtcp"},{"ev","ignoring RTCP packet coming from an invalid tuple"}});
    return QRPC_OK;
  }
  // Decrypt the SRTCP packet.
  auto intLen = static_cast<int>(sz);
  if (!srtp_recv_->DecryptSrtcp(const_cast<uint8_t *>(p), &intLen)) {
    logger::debug({{"proto","rtcp"},{"ev","fail to decrypt SRTCP packet"},
      {"pl",str::HexDump(p, std::min((size_t)32, sz))}});
    return QRPC_OK;
  }
  RTC::RTCP::Packet* packet = RTC::RTCP::Packet::Parse(p, static_cast<size_t>(intLen));
  if (!packet) {
    logger::warn({{"proto","rtcp"},
      {"ev","received data is not a valid RTCP compound or single packet"},
      {"pl",str::HexDump(p, std::min((size_t)32, sz))}});
    return QRPC_OK;
  }
  // we need to implement RTC::Transport::ReceiveRtcpPacket(packet) equivalent
  logger::error({{"ev","RTCP packet received, but handler not implemented yet"}});
  ASSERT(false);
  return QRPC_ENOTSUPPORT;
}
int WebRTCServer::Connection::OnRtpDataReceived(Session *session, const uint8_t *p, size_t sz) {
  TRACK();
  // Ensure DTLS is connected.
  if (dtls_transport_->GetState() != RTC::DtlsTransport::DtlsState::CONNECTED) {
    logger::debug({{"ev","ignoring RTCP packet while DTLS not connected"},{"proto","dtls,rtcp"}});
    return QRPC_OK;
  }
  // Ensure there is receiving SRTP session.
  if (srtp_recv_ != nullptr) {
    logger::debug({{"proto","srtp"},{"ev","ignoring RTCP packet due to non receiving SRTP session"}});
    return QRPC_OK;
  }
  // Ensure it comes from a valid tuple.
  if (!ice_server_->IsValidTuple(session)) {
    logger::warn({{"proto","rtcp"},{"ev","ignoring RTCP packet coming from an invalid tuple"}});
    return QRPC_OK;
  }
  // Decrypt the SRTP packet.
  auto intLen = static_cast<int>(sz);
  if (!this->srtp_recv_->DecryptSrtp(const_cast<uint8_t*>(p), &intLen)) {
    RTC::RtpPacket* packet = RTC::RtpPacket::Parse(p, static_cast<size_t>(intLen));
    if (packet == nullptr) {
      logger::warn({{"proto","srtp"},{"ev","DecryptSrtp() failed due to an invalid RTP packet"}});
    } else {
      logger::warn({
        {"proto","srtp"},
        {"ev","DecryptSrtp() failed"},
        {"ssrc",packet->GetSsrc()},
        {"payloadType",packet->GetPayloadType()},
        {"seq",packet->GetSequenceNumber()}
      });
      delete packet;
    }
    return QRPC_OK;
  }
  RTC::RtpPacket* packet = RTC::RtpPacket::Parse(p, static_cast<size_t>(intLen));
  if (packet == nullptr) {
    logger::warn({{"proto","rtcp"},
      {"ev","received data is not a valid RTP packet"},
      {"pl",str::HexDump(p, std::min((size_t)32, sz))}});
    return QRPC_OK;
  }
  // Trick for clients performing aggressive ICE regardless we are ICE-Lite.
  this->ice_server_->MayForceSelectedSession(session);
  // we need to implement RTC::Transport::ReceiveRtpPacket(packet); equivalent
  logger::error({{"ev","RTP packet received, but handler not implemented yet"}});
  ASSERT(false);
  return QRPC_ENOTSUPPORT;
}

// implements Stream::Processor
int WebRTCServer::Connection::Send(Stream *s, const char *p, size_t sz, bool binary) {
  PPID ppid = binary ? 
    (sz > 0 ? PPID::BINARY : PPID::BINARY_EMPTY) : 
    (sz > 0 ? PPID::STRING : PPID::STRING_EMPTY);
  sctp_association_->SendSctpMessage(s, ppid, reinterpret_cast<const uint8_t *>(p), sz);
  return QRPC_OK;
}
void WebRTCServer::Connection::Close(Stream *s) {
  sctp_association_->DataConsumerClosed(s);
}

// implements IceServer::Listener
void WebRTCServer::Connection::OnIceServerSendStunPacket(
  const IceServer *iceServer, const RTC::StunPacket* packet, Session *session) {
  TRACK();
  session->Send(reinterpret_cast<const char *>(packet->GetData()), packet->GetSize());
  // may need to implement equivalent
  // RTC::Transport::DataSent(packet->GetSize());
}
void WebRTCServer::Connection::OnIceServerLocalUsernameFragmentAdded(
  const IceServer *iceServer, const std::string& usernameFragment) {
  logger::info({{"ev","OnIceServerLocalUsernameFragmentAdded"},{"uflag",usernameFragment}});
  // mediasoup seems to add Connection itself to WebRTCServer's map here.
  // and OnIceServerLocalUsernameFragmentAdded is called from WebRtcTransport::ctor
  // thus, if mediasoup creates WebRtcTransport, it will be added to the map automatically
  // but it is too implicit. I rather prefer to add it manualy, in NewConnection
}
void WebRTCServer::Connection::OnIceServerLocalUsernameFragmentRemoved(
  const IceServer *iceServer, const std::string& usernameFragment) {
  logger::info({{"ev","OnIceServerLocalUsernameFragmentRemoved"},{"uflag",usernameFragment}});
  auto uflag = IceUFlag{usernameFragment};
  sv_.RemoveUFlag(uflag);
}
void WebRTCServer::Connection::OnIceServerSessionAdded(const IceServer *iceServer, Session *session) {
  logger::info({{"ev","OnIceServerSessionAdded"},{"ss",str::dptr(session)}});
  // used for synching server's session/address map. 
  // use OnIceServerTupleAdded to search mediasoup's example
}
void WebRTCServer::Connection::OnIceServerSessionRemoved(
  const IceServer *iceServer, Session *session) {
  logger::info({{"ev","OnIceServerSessionAdded"},{"ss",str::dptr(session)}});
  // used for synching server's session/address map. 
  // use OnIceServerTupleRemoved to search mediasoup's example
}
void WebRTCServer::Connection::OnIceServerSelectedSession(
  const IceServer *iceServer, Session *session) {
  TRACK();
  // just notify the app
  // use OnIceServerSelectedTuple to search mediasoup's example
}
void WebRTCServer::Connection::OnIceServerConnected(const IceServer *iceServer) {
  TRACK();
  // If ready, run the DTLS handler.
  RunDtlsTransport();

  // If DTLS was already connected, notify the parent class.
  if (dtls_transport_->GetState() == RTC::DtlsTransport::DtlsState::CONNECTED) {
    // TODO:
  }
}
void WebRTCServer::Connection::OnIceServerCompleted(const IceServer *iceServer) {
  TRACK();
}
void WebRTCServer::Connection::OnIceServerDisconnected(const IceServer *iceServer) {
  TRACK();
}

// implements IceServer::Listener
void WebRTCServer::Connection::OnDtlsTransportConnecting(const RTC::DtlsTransport* dtlsTransport) {
  TRACK();
}
void WebRTCServer::Connection::OnDtlsTransportConnected(
  const RTC::DtlsTransport* dtlsTransport,
  RTC::SrtpSession::CryptoSuite srtpCryptoSuite,
  uint8_t* srtpLocalKey,
  size_t srtpLocalKeyLen,
  uint8_t* srtpRemoteKey,
  size_t srtpRemoteKeyLen,
  std::string& remoteCert) {
  TRACK();
  // Close it if it was already set and update it.
  if (srtp_send_ != nullptr) {
    delete srtp_send_;
    srtp_send_ = nullptr;
  }
  if (srtp_recv_ != nullptr) {
    delete srtp_recv_;
    srtp_recv_ = nullptr;
  }
  try {
    srtp_send_ = new RTC::SrtpSession(
      RTC::SrtpSession::Type::OUTBOUND, srtpCryptoSuite, srtpLocalKey, srtpLocalKeyLen);
  } catch (const MediaSoupError& error) {
    logger::error({{"ev","error creating SRTP sending session"},{"reason",error.what()}});
  }
  try {
    srtp_recv_ = new RTC::SrtpSession(
      RTC::SrtpSession::Type::INBOUND, srtpCryptoSuite, srtpRemoteKey, srtpRemoteKeyLen);
    // may need to implement equivalent
    // RTC::Transport::Connected();
  } catch (const MediaSoupError& error) {
    logger::error({{"ev","error creating SRTP receiving session"},{"reason",error.what()}});
    delete srtp_send_;
    srtp_send_ = nullptr;
  }  
}
// The DTLS connection has been closed as the result of an error (such as a
// DTLS alert or a failure to validate the remote fingerprint).
void WebRTCServer::Connection::OnDtlsTransportFailed(const RTC::DtlsTransport* dtlsTransport) {
  logger::info({{"ev","tls failed"}});
  // TODO: callback
}
// The DTLS connection has been closed due to receipt of a close_notify alert.
void WebRTCServer::Connection::OnDtlsTransportClosed(const RTC::DtlsTransport* dtlsTransport) {
  logger::info({{"ev","tls cloed"}});
  // Tell the parent class.
  // RTC::Transport::Disconnected();
  // above notifies TransportCongestionControlClient and TransportCongestionControlServer
  // may need to implement equivalent for performance
}
// Need to send DTLS data to the peer.
void WebRTCServer::Connection::OnDtlsTransportSendData(
  const RTC::DtlsTransport* dtlsTransport, const uint8_t* data, size_t len) {
  TRACK();
  auto *session = ice_server_->GetSelectedSession();
  if (session == nullptr) {
    logger::warn({{"proto","dtls"},{"ev","no selected tuple set, cannot send DTLS packet"}});
    return;
  }
  session->Send(reinterpret_cast<const char *>(data), len);
  // may need to implement equivalent
  // RTC::Transport::DataSent(len);
}
// DTLS application data received.
void WebRTCServer::Connection::OnDtlsTransportApplicationDataReceived(
  const RTC::DtlsTransport*, const uint8_t* data, size_t len) {
  TRACK();
  sctp_association_->ProcessSctpData(data, len);
}

// implements SctpAssociation::Listener
void WebRTCServer::Connection::OnSctpAssociationConnecting(SctpAssociation* sctpAssociation) {
  TRACK();
  // only notify
}
void WebRTCServer::Connection::OnSctpAssociationConnected(SctpAssociation* sctpAssociation) {
  TRACK();
  sctp_connected_ = true;
}
void WebRTCServer::Connection::OnSctpAssociationFailed(SctpAssociation* sctpAssociation) {
  TRACK();
  sctp_connected_ = false;
  // TODO: notify app
}
void WebRTCServer::Connection::OnSctpAssociationClosed(SctpAssociation* sctpAssociation) {
  TRACK();
  sctp_connected_ = false;
  // TODO: notify app
}
void WebRTCServer::Connection::OnSctpAssociationSendData(
  SctpAssociation* sctpAssociation, const uint8_t* data, size_t len) {
  TRACK();
  if (!connected()) {
		logger::warn({{"proto","sctp"},{"ev","DTLS not connected, cannot send SCTP data"}});
    return;
  }	  
  dtls_transport_->SendApplicationData(data, len);
}
void WebRTCServer::Connection::OnSctpAssociationMessageReceived(
  SctpAssociation* sctpAssociation,
  uint16_t streamId,
  uint32_t ppid,
  const uint8_t* msg,
  size_t len) {
  TRACK();
  // TODO: callback app
  auto it = streams_.find(streamId);
  if (it == streams_.end()) {
    logger::debug({{"ev","SCTP message received for unknown stream, ignoring it"},{"sid",streamId}});
    return;
  }
  it->second.OnRead(reinterpret_cast<const char *>(msg), len);
}
void WebRTCServer::Connection::OnSctpAssociationBufferedAmount(
  SctpAssociation* sctpAssociation, uint32_t len) {
  TRACK();
}   
} //namespace base