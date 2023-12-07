#include "base/defs.h"
#include "base/crypto.h"
#include "base/webrtc.h"
#include "base/webrtc/sctp.h"
#include "base/webrtc/dcep.h"
#include "base/webrtc/sdp.h"

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
  if ((r = GlobalInit(config_.alarm_processor)) < 0) {
    return r;
  }
  if ((r = config_.Derive(config_.alarm_processor)) < 0) {
    return r;
  }
  // setup TCP/UDP ports
  for (auto port : config_.ports) {
    switch (port.protocol) {
      case Port::Protocol::UDP: {
        auto &p = udp_ports_.emplace_back(*this);
      if (!p.Listen(port.port)) {
          logger::error({{"ev","fail to listen"},{"port",port.port}});
          return r;
        }
      } break;
      case Port::Protocol::TCP: {
        auto &p = tcp_ports_.emplace_back(*this);
        if (!p.Listen(port.port)) {
          logger::error({{"ev","fail to listen"},{"port",port.port}});
          return r;
        }
      } break;
      default:
        logger::error({{"ev","unsupported protocol"},{"proto",port.protocol}});
        return QRPC_ENOTSUPPORT;
    }
  }
  if (config_.connection_timeout > 0) {
    alarm_processor().Set(
      [this]() { return this->CheckTimeout(); },
      qrpc_time_now() + config_.connection_timeout
    );
  }
  return QRPC_OK;
}
void WebRTCServer::Fin() {
  GlobalFin();
  if (alarm_id_ != AlarmProcessor::INVALID_ID) {
    alarm_processor().Cancel(alarm_id_);
    alarm_id_ = AlarmProcessor::INVALID_ID;
  }
  // cleanup TCP/UDP ports
}
void WebRTCServer::CloseConnection(Connection &c) {
  // how to close?
  logger::info({{"ev","close webrtc connection"},{"uflag",c.ice_server().GetUsernameFragment()}});
  connections_.erase(c.ice_server().GetUsernameFragment());
  // c might be freed here
}
int WebRTCServer::NewConnection(const std::string &client_sdp, std::string &server_sdp) {
  logger::info({{"ev","new connection"},{"client_sdp", client_sdp}});
  auto c = std::shared_ptr<Connection>(new Connection(*this, DtlsTransport::Role::SERVER));
  if (c == nullptr) {
    logger::error({{"ev","fail to allocate connection"}});
    return QRPC_EALLOC;
  }
  int r;
  std::string uflag, pwd;
  if ((r = c->Init(uflag, pwd)) < 0) {
    logger::error({{"ev","fail to init connection"},{"rc",r}});
    return QRPC_EINVAL;
  }
  SDP sdp(client_sdp);
  if (!sdp.Answer(*c, server_sdp)) {
    logger::error({{"ev","invalid client sdp"},{"sdp",client_sdp},{"reason",server_sdp}});
    return QRPC_EINVAL;
  }
  connections_.emplace(std::move(uflag), c);
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
std::shared_ptr<WebRTCServer::Connection> WebRTCServer::FindFromStunRequest(const uint8_t *p, size_t sz) {
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
  return it->second;
}

int WebRTCServer::GlobalInit(AlarmProcessor &a) {
	try
	{
		// Initialize static stuff.
		DepOpenSSL::ClassInit();
		DepLibSRTP::ClassInit();
		DepUsrSCTP::ClassInit(a);
		DepLibWebRTC::ClassInit();
		Utils::Crypto::ClassInit();
		DtlsTransport::ClassInit();
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
		DtlsTransport::ClassDestroy();
		DepUsrSCTP::ClassDestroy();
		DepLibUV::ClassDestroy();
  }
  catch (const MediaSoupError& error) {
    logger::error({{"ev","mediasoup cleanup failure"},{"reason",error.what()}});
	}
}

// WebRTCServer::Config
class DummyDtlsTransportListener : public DtlsTransport::Listener {
  void OnDtlsTransportConnecting(const DtlsTransport*) override {}
  void OnDtlsTransportConnected(
    const DtlsTransport*,
    RTC::SrtpSession::CryptoSuite,
    uint8_t*,
    size_t,
    uint8_t*,
    size_t,
    std::string&) override {}
  void OnDtlsTransportFailed(const DtlsTransport*) override {}
  void OnDtlsTransportClosed(const DtlsTransport*) override {}
  void OnDtlsTransportSendData(
    const DtlsTransport*, const uint8_t*, size_t) override {}
  void OnDtlsTransportApplicationDataReceived(
    const DtlsTransport*, const uint8_t*, size_t) override {}
};
int WebRTCServer::Config::Derive(AlarmProcessor &ap) {
  for (auto fp : DtlsTransport::GetLocalFingerprints()) {
    // TODO: SHA256 is enough?
    if (fp.algorithm == DtlsTransport::GetFingerprintAlgorithm(fingerprint_algorithm)) {
      fingerprint = fp.value;
      return QRPC_OK;
    }
  }
  logger::die({{"ev","no fingerprint for algorithm found"},{"algo", fingerprint_algorithm}}); // should not happen
  return QRPC_EDEPS;
}

// WebRTCServer::UdpSession/TcpSession
int WebRTCServer::TcpSession::OnRead(const char *p, size_t sz) {
  auto up = reinterpret_cast<const uint8_t *>(p);
  if (connection_ == nullptr) {
    connection_ = factory().to<TcpPort>().webrtc_server().FindFromStunRequest(up, sz);
    if (connection_ == nullptr) {
      QRPC_LOGJ(info, {{"ev","fail to find connection from stun request"}})
      return QRPC_EINVAL;
    }
  }
  return connection_->OnPacketReceived(this, up, sz);
}
void WebRTCServer::TcpSession::OnShutdown() {
  if (connection_ != nullptr) {
    connection_->OnTcpSessionShutdown(this);
  }
}
int WebRTCServer::UdpSession::OnRead(const char *p, size_t sz) {
  auto up = reinterpret_cast<const uint8_t *>(p);
  if (connection_ == nullptr) {
    connection_ = factory().to<UdpPort>().webrtc_server().FindFromStunRequest(up, sz);
    if (connection_ == nullptr) {
      QRPC_LOGJ(info, {{"ev","fail to find connection from stun request"}})
      return QRPC_EINVAL;
    }
  }
  return connection_->OnPacketReceived(this, up, sz);
}
void WebRTCServer::UdpSession::OnShutdown() {
  if (connection_ != nullptr) {
    connection_->OnUdpSessionShutdown(this);
  }
}


/* WebRTCServer::Connection */
bool WebRTCServer::Connection::connected() const {
  return (
    (
      ice_server_->GetState() == IceServer::IceState::CONNECTED ||
      ice_server_->GetState() == IceServer::IceState::COMPLETED
    ) && dtls_transport_->GetState() == DtlsTransport::DtlsState::CONNECTED
  );
}
int WebRTCServer::Connection::Init(std::string &uflag, std::string &pwd) {
  if (ice_server_ != nullptr) {
    logger::warn({{"ev","already init"}});
    return QRPC_OK;
  }
  uflag = random::word(32);
  pwd = random::word(32);
  // create ICE server
  ice_server_.reset(new IceServer(this, uflag, pwd));
  if (ice_server_ == nullptr) {
    logger::die({{"ev","fail to create ICE server"}});
    return QRPC_EALLOC;
  }
  // create DTLS transport
  try {
    dtls_transport_.reset(new DtlsTransport(this, server().alarm_processor()));
  } catch (const MediaSoupError &error) {
    logger::die({{"ev","fail to create DTLS transport"},{"reason",error.what()}});
    return QRPC_EALLOC;
  }
  // create SCTP association
  sctp_association_.reset(
    new SctpAssociation(
      this, 
      server().config().max_outgoing_stream_size,
      server().config().initial_incoming_stream_size,
      server().config().sctp_send_buffer_size,
      server().config().sctp_send_buffer_size,
      true)
  );
  if (sctp_association_ == nullptr) {
    logger::die({{"ev","fail to create SCTP association"}});
    return QRPC_EALLOC;
  }
  return QRPC_OK;
}
std::shared_ptr<Stream> WebRTCServer::Connection::NewStream(Stream::Config &c) {
  if (streams_.find(c.params.streamId) != streams_.end()) {
    ASSERT(false);
    logger::error({{"ev","stream id already used"},{"sid",c.params.streamId}});
    return nullptr;
  }
  auto s = server().stream_factory()(c, *this);
  if (s == nullptr) {
    ASSERT(false);
    logger::error({{"ev","fail to create stream"},{"sid",c.params.streamId}});
    return nullptr;
  }
  logger::info({{"ev","new stream created"},{"sid",s->id()}});
  streams_.emplace(s->id(), s);
  return s;
}
std::shared_ptr<Stream> WebRTCServer::Connection::OpenStream(Stream::Config &c) {
  int r;
  size_t cnt = 0;
  do {
    // auto allocate
    c.params.streamId = stream_id_factory_.New();
  } while (streams_.find(c.params.streamId) != streams_.end() && ++cnt <= 0xFFFF);
  if (cnt > 0xFFFF) {
    ASSERT(false);
    logger::error({{"ev","cannot allocate stream id"}});
    return nullptr;
  }
  auto s = NewStream(c);
  if (s == nullptr) { return nullptr; }
  // allocate stream Id
  sctp_association_->HandleDataConsumer(s.get());
  // TODO: send DCEP OPEN messsage to peer
  if ((r = s->Open()) < 0) {
    logger::info({{"ev","new stream creation blocked"},{"sid",s->id()},{"rc",r}});
    Close(*s);
    return nullptr;
  }
  logger::info({{"ev","new stream opened"},{"sid",s->id()}});
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
    case DtlsTransport::Role::AUTO: {
      if (
        ice_server_->GetState() == IceServer::IceState::CONNECTED ||
        ice_server_->GetState() == IceServer::IceState::COMPLETED
      ) {
        logger::info(
          {{"proto","dtls"},{"ev","transition from DTLS local role 'auto' to 'server' and running DTLS transport"}}
        );
        dtls_role_ = DtlsTransport::Role::SERVER;
        dtls_transport_->Run(DtlsTransport::Role::SERVER);
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
    case DtlsTransport::Role::CLIENT: {
      if (
        ice_server_->GetState() == IceServer::IceState::CONNECTED ||
        ice_server_->GetState() == IceServer::IceState::COMPLETED
      ) {
        logger::debug({{"proto","dtls"},{"ev","running DTLS transport in local role 'client'"}});
        dtls_transport_->Run(DtlsTransport::Role::CLIENT);
      }
      break;
    }

    // If 'server' then run the DTLS transport if ICE is 'connected' (not yet
    // USE-CANDIDATE) or 'completed'.
    case DtlsTransport::Role::SERVER: {
      if (
        ice_server_->GetState() == IceServer::IceState::CONNECTED ||
        ice_server_->GetState() == IceServer::IceState::COMPLETED
      ) {
        logger::debug({{"proto","dtls"},{"ev","running DTLS transport in local role 'server'"}});
        dtls_transport_->Run(DtlsTransport::Role::SERVER);
      }
      break;
    }

    default: logger::die({{"ev","invalid local DTLS role"},{"role",dtls_role_}});
  }
  return QRPC_OK;
}
void WebRTCServer::Connection::OnDtlsEstablished() {
  sctp_association_->TransportConnected();
}
void WebRTCServer::Connection::OnTcpSessionShutdown(Session *s) {
  ice_server_->RemoveTuple(s);
}
void WebRTCServer::Connection::OnUdpSessionShutdown(Session *s) {
  ice_server_->RemoveTuple(s);
}

int WebRTCServer::Connection::OnPacketReceived(Session *session, const uint8_t *p, size_t sz) {
  // Check if it's STUN.
  if (RTC::StunPacket::IsStun(p, sz)) {
    return OnStunDataReceived(session, p, sz);
  } else if (DtlsTransport::IsDtls(p, sz)) { // Check if it's DTLS.
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
  Touch(qrpc_time_now());
  // Trick for clients performing aggressive ICE regardless we are ICE-Lite.
  ice_server_->MayForceSelectedSession(session);
  // Check that DTLS status is 'connecting' or 'connected'.
  if (
    dtls_transport_->GetState() == DtlsTransport::DtlsState::CONNECTING ||
    dtls_transport_->GetState() == DtlsTransport::DtlsState::CONNECTED) {
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
  if (dtls_transport_->GetState() != DtlsTransport::DtlsState::CONNECTED) {
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
  if (dtls_transport_->GetState() != DtlsTransport::DtlsState::CONNECTED) {
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
int WebRTCServer::Connection::Send(Stream &s, const char *p, size_t sz, bool binary) {
  PPID ppid = binary ? 
    (sz > 0 ? PPID::BINARY : PPID::BINARY_EMPTY) : 
    (sz > 0 ? PPID::STRING : PPID::STRING_EMPTY);
  sctp_association_->SendSctpMessage(&s, ppid, reinterpret_cast<const uint8_t *>(p), sz);
  return QRPC_OK;
}
void WebRTCServer::Connection::Close(Stream &s) {
  sctp_association_->DataConsumerClosed(&s);
  streams_.erase(s.id()); // s might destroyed
}
int WebRTCServer::Connection::Open(Stream &s) {
  auto &c = s.config();
  DcepRequest req(c);
  uint8_t buff[req.PayloadSize()];
  if (sctp_association_->SendSctpMessage(
      &s, PPID::WEBRTC_DCEP, req.ToPaylod(buff, sizeof(buff)), req.PayloadSize()
  ) < 0) {
    logger::error({{"proto","sctp"},{"ev","fail to send DCEP ACK"},{"stream_id",s.id()}});
    Close(s);
    return QRPC_EALLOC;
  }
  return QRPC_OK;
}

// implements IceServer::Listener
void WebRTCServer::Connection::OnIceServerSendStunPacket(
  const IceServer *iceServer, const RTC::StunPacket* packet, Session *session) {
  // TRACK();
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
  if (dtls_transport_->GetState() == DtlsTransport::DtlsState::CONNECTED) {
    OnDtlsEstablished();
  }
}
void WebRTCServer::Connection::OnIceServerCompleted(const IceServer *iceServer) {
  TRACK();
  OnIceServerConnected(iceServer);
}
void WebRTCServer::Connection::OnIceServerDisconnected(const IceServer *iceServer) {
  TRACK();
}

// implements IceServer::Listener
void WebRTCServer::Connection::OnDtlsTransportConnecting(const DtlsTransport* dtlsTransport) {
  TRACK();
}
void WebRTCServer::Connection::OnDtlsTransportConnected(
  const DtlsTransport* dtlsTransport,
  RTC::SrtpSession::CryptoSuite srtpCryptoSuite,
  uint8_t* srtpLocalKey,
  size_t srtpLocalKeyLen,
  uint8_t* srtpRemoteKey,
  size_t srtpRemoteKeyLen,
  std::string& remoteCert) {
  TRACK();
  // Close it if it was already set and update it.
  // old pointer will be deleted by unique_ptr.reset()
  try {
    srtp_send_.reset(new RTC::SrtpSession(
      RTC::SrtpSession::Type::OUTBOUND, srtpCryptoSuite, srtpLocalKey, srtpLocalKeyLen));
  } catch (const MediaSoupError& error) {
    logger::error({{"ev","error creating SRTP sending session"},{"reason",error.what()}});
  }
  try {
    srtp_recv_.reset(new RTC::SrtpSession(
      RTC::SrtpSession::Type::INBOUND, srtpCryptoSuite, srtpRemoteKey, srtpRemoteKeyLen));
    OnDtlsEstablished();
  } catch (const MediaSoupError& error) {
    logger::error({{"ev","error creating SRTP receiving session"},{"reason",error.what()}});
    srtp_send_.reset();
  }  
}
// The DTLS connection has been closed as the result of an error (such as a
// DTLS alert or a failure to validate the remote fingerprint).
void WebRTCServer::Connection::OnDtlsTransportFailed(const DtlsTransport* dtlsTransport) {
  logger::info({{"ev","tls failed"}});
  // TODO: callback
}
// The DTLS connection has been closed due to receipt of a close_notify alert.
void WebRTCServer::Connection::OnDtlsTransportClosed(const DtlsTransport* dtlsTransport) {
  logger::info({{"ev","tls cloed"}});
  // Tell the parent class.
  // RTC::Transport::Disconnected();
  // above notifies TransportCongestionControlClient and TransportCongestionControlServer
  // may need to implement equivalent for performance
}
// Need to send DTLS data to the peer.
void WebRTCServer::Connection::OnDtlsTransportSendData(
  const DtlsTransport* dtlsTransport, const uint8_t* data, size_t len) {
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
  const DtlsTransport*, const uint8_t* data, size_t len) {
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
		logger::warn({{"proto","sctp"},{"ev","DTLS not connected, cannot send SCTP data"},
      {"dtls_state",dtls_transport_->GetState()}});
    ASSERT(false);
    return;
  }
  dtls_transport_->SendApplicationData(data, len);
}
void WebRTCServer::Connection::OnSctpWebRtcDataChannelControlDataReceived(
  SctpAssociation* sctpAssociation,
  uint16_t streamId,
  const uint8_t* msg,
  size_t len) {
  // parse msg and create Stream::Config from it, then create stream by using NewStream
  TRACK();
  switch (*msg) {
  case DATA_CHANNEL_ACK: {
    auto s = streams_.find(streamId);
    if (s == streams_.end()) {
      logger::error({{"proto","sctp"},{"ev","DATA_CHANNEL_ACK received for unknown stream"}});
      return;
    }
    if (s->second->OnConnect() < 0) {
      logger::error({{"proto","sctp"},{"ev","DATA_CHANNEL_ACK blocked for application reason"}});
      Close(*s->second);
      return;
    }
  } break;
  case DATA_CHANNEL_OPEN: {
    auto req = DcepRequest::Parse(streamId, msg, len);
    if (req == nullptr) {
      logger::error({{"proto","sctp"},{"ev","invalid DCEP request received"}});
      return;
    }
    auto c = req->ToStreamConfig();
    auto s = NewStream(c);
    if (s == nullptr) {
      logger::error({{"proto","sctp"},{"ev","fail to create stream"},{"stream_id",streamId}});
      return;
    }
    // send dcep ack
    DcepResponse ack;
    uint8_t buff[ack.PayloadSize()];
    if (sctpAssociation->SendSctpMessage(
        s.get(), PPID::WEBRTC_DCEP, ack.ToPaylod(buff, sizeof(buff)), ack.PayloadSize()
    ) < 0) {
      logger::error({{"proto","sctp"},{"ev","fail to send DCEP ACK"},{"stream_id",streamId}});
      Close(*s);
      return;
    }
  } break;
  }
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
  it->second->OnRead(reinterpret_cast<const char *>(msg), len);
}
void WebRTCServer::Connection::OnSctpAssociationBufferedAmount(
  SctpAssociation* sctpAssociation, uint32_t len) {
  TRACK();
}   
} //namespace base