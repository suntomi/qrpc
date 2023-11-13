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

#include <mutex>

namespace base {
static std::once_flag once_init, once_fin;

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