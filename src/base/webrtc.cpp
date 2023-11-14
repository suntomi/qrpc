#include "base/defs.h"
#include "base/webrtc.h"

#include "common.hpp"
#include "MediaSoupErrors.hpp"
#include "DepLibSRTP.hpp"
#include "DepLibUV.hpp"
#include "DepLibWebRTC.hpp"
#include "DepOpenSSL.hpp"
#include "DepUsrSCTP.hpp"
#include "RTC/DtlsTransport.hpp"
#include "RTC/SrtpSession.hpp"
#include "RTC/StunPacket.hpp"
#include "RTC/RtpPacket.hpp"

#include <mutex>
#include <algorithm>

namespace base {
static std::once_flag once_init, once_fin;

// WebRTCServer
int WebRTCServer::Init() {
  std::call_once(once_init, GlobalInit);
  return QRPC_OK;
}
void WebRTCServer::Fin() {
  std::call_once(once_fin, GlobalFin);
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

int WebRTCServer::GlobalInit() {
	try
	{
		// Initialize static stuff.
		DepOpenSSL::ClassInit();
		DepLibSRTP::ClassInit();
		DepUsrSCTP::ClassInit();
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
// implements IceServer::Listener
void WebRTCServer::Connection::OnIceServerSendStunPacket(
  const IceServer *iceServer, const RTC::StunPacket* packet, Session *session) {
  TRACK();
}
void WebRTCServer::Connection::OnIceServerLocalUsernameFragmentAdded(
  const IceServer *iceServer, const std::string& usernameFragment) {
  logger::info({{"ev","OnIceServerLocalUsernameFragmentAdded"},{"uflag",usernameFragment}});
}
void WebRTCServer::Connection::OnIceServerLocalUsernameFragmentRemoved(
  const IceServer *iceServer, const std::string& usernameFragment) {
  logger::info({{"ev","OnIceServerLocalUsernameFragmentRemoved"},{"uflag",usernameFragment}});
  auto uflag = IceUFlag{usernameFragment};
  sv_.RemoveUFlag(uflag);
}
void WebRTCServer::Connection::OnIceServerSessionAdded(const IceServer *iceServer, Session *session) {
  logger::info({{"ev","OnIceServerSessionAdded"},{"ss",str::dptr(session)}});
}
void WebRTCServer::Connection::OnIceServerSessionRemoved(
  const IceServer *iceServer, Session *session) {
  logger::info({{"ev","OnIceServerSessionAdded"},{"ss",str::dptr(session)}});
}
void WebRTCServer::Connection::OnIceServerSelectedSession(
  const IceServer *iceServer, Session *session) {
  TRACK();
}
void WebRTCServer::Connection::OnIceServerConnected(const IceServer *iceServer) {
  TRACK();
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
}
// The DTLS connection has been closed as the result of an error (such as a
// DTLS alert or a failure to validate the remote fingerprint).
void WebRTCServer::Connection::OnDtlsTransportFailed(const RTC::DtlsTransport* dtlsTransport) {
  TRACK();
}
// The DTLS connection has been closed due to receipt of a close_notify alert.
void WebRTCServer::Connection::OnDtlsTransportClosed(const RTC::DtlsTransport* dtlsTransport) {
  TRACK();
}
// Need to send DTLS data to the peer.
void WebRTCServer::Connection::OnDtlsTransportSendData(
  const RTC::DtlsTransport* dtlsTransport, const uint8_t* data, size_t len) {
  TRACK();
}
// DTLS application data received.
void WebRTCServer::Connection::OnDtlsTransportApplicationDataReceived(
  const RTC::DtlsTransport* dtlsTransport, const uint8_t* data, size_t len) {
  TRACK();
}

// implements RTC::SctpAssociation::Listener
void WebRTCServer::Connection::OnSctpAssociationConnecting(RTC::SctpAssociation* sctpAssociation) {
  TRACK();
}
void WebRTCServer::Connection::OnSctpAssociationConnected(RTC::SctpAssociation* sctpAssociation) {
  TRACK();
}
void WebRTCServer::Connection::OnSctpAssociationFailed(RTC::SctpAssociation* sctpAssociation) {
  TRACK();
}
void WebRTCServer::Connection::OnSctpAssociationClosed(RTC::SctpAssociation* sctpAssociation) {
  TRACK();
}
void WebRTCServer::Connection::OnSctpAssociationSendData(
  RTC::SctpAssociation* sctpAssociation, const uint8_t* data, size_t len) {
  TRACK();
}
void WebRTCServer::Connection::OnSctpAssociationMessageReceived(
  RTC::SctpAssociation* sctpAssociation,
  uint16_t streamId,
  uint32_t ppid,
  const uint8_t* msg,
  size_t len) {
  TRACK();
}
void WebRTCServer::Connection::OnSctpAssociationBufferedAmount(
  RTC::SctpAssociation* sctpAssociation, uint32_t len) {
  TRACK();
}   
} //namespace base