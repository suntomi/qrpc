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
namespace webrtc {

// ConnectionFactory
int ConnectionFactory::Init() {
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
void ConnectionFactory::Fin() {
  if (alarm_id_ != AlarmProcessor::INVALID_ID) {
    alarm_processor().Cancel(alarm_id_);
    alarm_id_ = AlarmProcessor::INVALID_ID;
  }
  for (auto &p : udp_ports_) {
    p.Fin();
  }
  for (auto &p : tcp_ports_) {
    p.Fin();
  }
  GlobalFin();
}
void ConnectionFactory::CloseConnection(Connection &c) {
  // how to close?
  logger::info({{"ev","close webrtc connection"},{"uflag",c.ice_server().GetUsernameFragment()}});
  c.Fin(); // cleanup resources if not yet
  connections_.erase(c.ice_server().GetUsernameFragment());
  // c might be freed here
}
static inline ConnectionFactory::IceUFlag GetLocalIceUFragFrom(RTC::StunPacket* packet) {
		TRACK();

		// Here we inspect the USERNAME attribute of a received STUN request and
		// extract its remote usernameFragment (the one given to our IceServer as
		// local usernameFragment) which is the first value in the attribute value
		// before the ":" symbol.

		const auto& username  = packet->GetUsername();
		const size_t colonPos = username.find(':');

		// If no colon is found just return the whole USERNAME attribute anyway.
		if (colonPos == std::string::npos) {
			return ConnectionFactory::IceUFlag{username};
    }

		return ConnectionFactory::IceUFlag{username.substr(0, colonPos)};
}
std::shared_ptr<ConnectionFactory::Connection>
ConnectionFactory::FindFromStunRequest(const uint8_t *p, size_t sz) {
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
std::shared_ptr<ConnectionFactory::Connection>
ConnectionFactory::FindFromUflag(const IceUFlag &uflag) {
    auto it = connections_.find(uflag);
    if (it == this->connections_.end()) {
      return nullptr;
    }
    return it->second;
}
int ConnectionFactory::GlobalInit(AlarmProcessor &a) {
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
void ConnectionFactory::GlobalFin() {
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

// ConnectionFactory::Config
int ConnectionFactory::Config::Derive(AlarmProcessor &ap) {
  for (auto fp : DtlsTransport::GetLocalFingerprints()) {
    // TODO: SHA256 is enough?
    if (fp.algorithm == DtlsTransport::GetFingerprintAlgorithm(fingerprint_algorithm)) {
      fingerprint = fp.value;
    }
  }
  if (fingerprint.length() <= 0) {
    logger::die({{"ev","no fingerprint for algorithm"},{"algo", fingerprint_algorithm}});
    return QRPC_EDEPS;
  }
  if (ip.length() <= 0) {
    for (auto &a : Syscall::GetIfAddrs()) {
      if (in6 == (a.family() == AF_INET6)) {
        logger::info({{"ev","add detected ip"},{"ip",a.hostip()},{"in6",in6}});
        ifaddrs.push_back(a.hostip());
      }
    }
  } else {
    logger::info({{"ev","add configured ip"},{"ip",ip}});
    ifaddrs.push_back(ip);
  }
  return QRPC_OK;
}

// ConnectionFactory::UdpSession/TcpSession
int ConnectionFactory::TcpSession::OnRead(const char *p, size_t sz) {
  auto up = reinterpret_cast<const uint8_t *>(p);
  if (connection_ == nullptr) {
    connection_ = factory().to<TcpPort>().connection_factory().FindFromStunRequest(up, sz);
    if (connection_ == nullptr) {
      QRPC_LOGJ(info, {{"ev","fail to find connection from stun request"}});
      return QRPC_EINVAL;
    }
  } else if (connection_->closed()) {
    QRPC_LOGJ(info, {{"ev","parent connection closed, remove the session"},{"from",addr().str()}});
    return QRPC_EGOAWAY;
  }
  return connection_->OnPacketReceived(this, up, sz);
}
qrpc_time_t ConnectionFactory::TcpSession::OnShutdown() {
  if (connection_ != nullptr) {
    connection_->OnTcpSessionShutdown(this);
  }
  return 0;
}
int ConnectionFactory::UdpSession::OnRead(const char *p, size_t sz) {
  auto up = reinterpret_cast<const uint8_t *>(p);
  if (connection_ == nullptr) {
    connection_ = factory().to<UdpPort>().connection_factory().FindFromStunRequest(up, sz);
    if (connection_ == nullptr) {
      QRPC_LOGJ(info, {{"ev","fail to find connection from stun request"}});
      return QRPC_EINVAL;
    }
  }
  return connection_->OnPacketReceived(this, up, sz);
}
qrpc_time_t ConnectionFactory::UdpSession::OnShutdown() {
  if (connection_ != nullptr) {
    connection_->OnUdpSessionShutdown(this);
  }
  return 0;
}


/* ConnectionFactory::Connection */
bool ConnectionFactory::Connection::connected() const {
  return (
    (
      ice_server_->GetState() == IceServer::IceState::CONNECTED ||
      ice_server_->GetState() == IceServer::IceState::COMPLETED
    ) && dtls_transport_->GetState() == DtlsTransport::DtlsState::CONNECTED
  );
}
int ConnectionFactory::Connection::Init(std::string &uflag, std::string &pwd) {
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
    dtls_transport_.reset(new DtlsTransport(this, factory().alarm_processor()));
  } catch (const MediaSoupError &error) {
    logger::error({{"ev","fail to create DTLS transport"},{"reason",error.what()}});
    return QRPC_EALLOC;
  }
  // create SCTP association
  sctp_association_.reset(
    new SctpAssociation(
      this, 
      factory().config().max_outgoing_stream_size,
      factory().config().initial_incoming_stream_size,
      factory().config().send_buffer_size,
      factory().config().send_buffer_size,
      true)
  );
  if (sctp_association_ == nullptr) {
    logger::die({{"ev","fail to create SCTP association"}});
    return QRPC_EALLOC;
  }
  return QRPC_OK;
}
std::shared_ptr<Stream> ConnectionFactory::Connection::NewStream(
  const Stream::Config &c, const StreamFactory &sf
) {
  if (streams_.find(c.params.streamId) != streams_.end()) {
    ASSERT(false);
    logger::error({{"ev","stream id already used"},{"sid",c.params.streamId}});
    return nullptr;
  }
  auto s = sf(c, *this);
  if (s == nullptr) {
    ASSERT(false);
    logger::error({{"ev","fail to create stream"},{"sid",c.params.streamId}});
    return nullptr;
  }
  logger::info({{"ev","new stream created"},{"sid",s->id()},{"l",s->label()}});
  streams_.emplace(s->id(), s);
  return s;
}
std::shared_ptr<Stream> ConnectionFactory::Connection::OpenStream(
  const Stream::Config &c, const StreamFactory &sf
) {
  int r;
  size_t cnt = 0;
  do {
    // auto allocate
    const_cast<Stream::Config &>(c).params.streamId = stream_id_factory_.New();
  } while (streams_.find(c.params.streamId) != streams_.end() && ++cnt <= 0xFFFF);
  if (cnt > 0xFFFF) {
    ASSERT(false);
    logger::error({{"ev","cannot allocate stream id"}});
    return nullptr;
  }
  auto s = NewStream(c, sf);
  if (s == nullptr) { return nullptr; }
  // allocate stream Id
  sctp_association_->HandleDataConsumer(s.get());
  // TODO: send DCEP OPEN messsage to peer
  if ((r = s->Open()) < 0) {
    logger::info({{"ev","new stream creation blocked"},{"sid",s->id()},{"rc",r}});
    s->Close(QRPC_CLOSE_REASON_LOCAL, r, "DCEP OPEN failed");
    return nullptr;
  }
  logger::info({{"ev","new stream opened"},{"sid",s->id()},{"l",s->label()}});
  return s;
}
void ConnectionFactory::Connection::Fin() {
  if (dtls_transport_ != nullptr) {
    dtls_transport_->Close();
  }
  for (auto &s : streams_) {
    s.second->OnShutdown();
  }
  OnShutdown();
  streams_.clear();
}
void ConnectionFactory::Connection::Close() {
  if (closed()) {
    return;
  }
  closed_ = true;
  OpenStream({
    .label = "$syscall"
  }, [](const Stream::Config &config, base::Connection &conn) {
    return std::make_shared<SyscallStream>(conn, config, [](Stream &s) {
      return s.Send({{"fn","close"}});
    });
  });
}
int ConnectionFactory::Connection::RunIceProber(
  Session *s, const std::string &uflag, const std::string &pwd) {
  TRACK();
  if (!ice_prober_) {
    ice_prober_ = std::make_unique<IceProber>(*this, uflag, pwd);
  }
  if (ice_server_->GetSelectedSession() == nullptr) {
      ice_server_->ForceSetSelectedSession(s);
  }
  ice_prober_->Start(factory().alarm_processor());
  return QRPC_OK;
}
int ConnectionFactory::Connection::RunDtlsTransport() {
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

    default: {
      logger::error({{"ev","invalid local DTLS role"},{"role",dtls_role_}});
      return QRPC_EINVAL;
    }
  }
  return QRPC_OK;
}
void ConnectionFactory::Connection::OnDtlsEstablished() {
  sctp_association_->TransportConnected();
  int r;
  if ((r = OnConnect()) < 0) {
    logger::error({{"ev","application reject connection"},{"rc",r}});
    factory().CloseConnection(*this);
  }
}
void ConnectionFactory::Connection::OnTcpSessionShutdown(Session *s) {
  ice_server_->RemoveTuple(s);
}
void ConnectionFactory::Connection::OnUdpSessionShutdown(Session *s) {
  ice_server_->RemoveTuple(s);
}

int ConnectionFactory::Connection::OnPacketReceived(Session *session, const uint8_t *p, size_t sz) {
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
int ConnectionFactory::Connection::OnStunDataReceived(Session *session, const uint8_t *p, size_t sz) {
  RTC::StunPacket* packet = RTC::StunPacket::Parse(p, sz);
  if (packet == nullptr) {
    logger::warn({{"ev","ignoring wrong STUN packet received"},{"proto","stun"}});
    return QRPC_OK;
  }
  ice_server_->ProcessStunPacket(packet, session);
  delete packet;
  return QRPC_OK;
}
int ConnectionFactory::Connection::OnDtlsDataReceived(Session *session, const uint8_t *p, size_t sz) {
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
    // logger::debug({{"ev","DTLS data received, passing it to the DTLS transport"},{"proto","dtls"}});
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
int ConnectionFactory::Connection::OnRtcpDataReceived(Session *session, const uint8_t *p, size_t sz) {
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
int ConnectionFactory::Connection::OnRtpDataReceived(Session *session, const uint8_t *p, size_t sz) {
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
int ConnectionFactory::Connection::Send(Stream &s, const char *p, size_t sz, bool binary) {
  PPID ppid = binary ? 
    (sz > 0 ? PPID::BINARY : PPID::BINARY_EMPTY) : 
    (sz > 0 ? PPID::STRING : PPID::STRING_EMPTY);
  sctp_association_->SendSctpMessage(&s, ppid, reinterpret_cast<const uint8_t *>(p), sz);
  return QRPC_OK;
}
int ConnectionFactory::Connection::Send(const char *p, size_t sz) {
  auto *session = ice_server_->GetSelectedSession();
  if (session == nullptr) {
    logger::warn({{"proto","raw"},{"ev","no selected tuple set, cannot send raw packet"}});
    return QRPC_EINVAL;
  }
  return session->Send(p, sz);
}

void ConnectionFactory::Connection::Close(Stream &s) {
  sctp_association_->DataConsumerClosed(&s);
  streams_.erase(s.id()); // s might destroyed
}
int ConnectionFactory::Connection::Open(Stream &s) {
  int r;
  auto &c = s.config();
  DcepRequest req(c);
  uint8_t buff[req.PayloadSize()];
  if ((r = sctp_association_->SendSctpMessage(
      &s, PPID::WEBRTC_DCEP, req.ToPaylod(buff, sizeof(buff)), req.PayloadSize()
  )) < 0) {
    logger::error({{"proto","sctp"},{"ev","fail to send DCEP OPEN"},{"stream_id",s.id()}});
    return QRPC_EALLOC;
  }
  return QRPC_OK;
}

// implements IceServer::Listener
void ConnectionFactory::Connection::OnIceServerSendStunPacket(
  const IceServer *iceServer, const RTC::StunPacket* packet, Session *session) {
  // TRACK();
  session->Send(reinterpret_cast<const char *>(packet->GetData()), packet->GetSize());
  // may need to implement equivalent
  // RTC::Transport::DataSent(packet->GetSize());
}
void ConnectionFactory::Connection::OnIceServerLocalUsernameFragmentAdded(
  const IceServer *iceServer, const std::string& usernameFragment) {
  logger::info({{"ev","OnIceServerLocalUsernameFragmentAdded"},{"uflag",usernameFragment}});
  // mediasoup seems to add Connection itself to ConnectionFactory's map here.
  // and OnIceServerLocalUsernameFragmentAdded is called from WebRtcTransport::ctor
  // thus, if mediasoup creates WebRtcTransport, it will be added to the map automatically
  // but it is too implicit. I rather prefer to add it manualy, in NewConnection
}
void ConnectionFactory::Connection::OnIceServerLocalUsernameFragmentRemoved(
  const IceServer *iceServer, const std::string& usernameFragment) {
  logger::info({{"ev","OnIceServerLocalUsernameFragmentRemoved"},{"uflag",usernameFragment}});
  auto uflag = IceUFlag{usernameFragment};
  sv_.CloseConnection(uflag);
}
void ConnectionFactory::Connection::OnIceServerSessionAdded(const IceServer *iceServer, Session *session) {
  logger::info({{"ev","OnIceServerSessionAdded"},{"ss",str::dptr(session)}});
  // used for synching server's session/address map. 
  // use OnIceServerTupleAdded to search mediasoup's example
}
void ConnectionFactory::Connection::OnIceServerSessionRemoved(
  const IceServer *iceServer, Session *session) {
  logger::info({{"ev","OnIceServerSessionAdded"},{"ss",str::dptr(session)}});
  // used for synching server's session/address map. 
  // use OnIceServerTupleRemoved to search mediasoup's example
}
void ConnectionFactory::Connection::OnIceServerSelectedSession(
  const IceServer *iceServer, Session *session) {
  TRACK();
  // just notify the app
  // use OnIceServerSelectedTuple to search mediasoup's example
}
void ConnectionFactory::Connection::OnIceServerConnected(const IceServer *iceServer) {
  TRACK();
  // If ready, run the DTLS handler.
  if (RunDtlsTransport() < 0) {
    logger::error({{"ev","fail to run DTLS transport"}});
    factory().CloseConnection(*this);
    return;
  }

  // If DTLS was already connected, notify the parent class.
  if (dtls_transport_->GetState() == DtlsTransport::DtlsState::CONNECTED) {
    OnDtlsEstablished();
  }
}
void ConnectionFactory::Connection::OnIceServerCompleted(const IceServer *iceServer) {
  TRACK();
  OnIceServerConnected(iceServer);
}
void ConnectionFactory::Connection::OnIceServerDisconnected(const IceServer *iceServer) {
  TRACK();
}
void ConnectionFactory::Connection::OnIceServerSuccessResponded(
  const IceServer *iceServer, const RTC::StunPacket* packet, Session *session) {
  if (!ice_prober_ || dtls_role_ != DtlsTransport::Role::CLIENT) {
    logger::warn({{"ev","stun packet response receive with invalid state"},{"dtls_role",dtls_role_}});
    ASSERT(false);
    return;
  }
  if (!ice_prober_->active()) {
    // stun binding request success. start dtls transport so that it can process
    // dtls handshake packets from server.
    int r;
    if ((r = RunDtlsTransport()) < 0) {
      logger::error({{"ev","fail to run dtls transport"},{"rc",r}});
      return;
    }
  }
  ice_prober_->Success();
}
void ConnectionFactory::Connection::OnIceServerErrorResponded(
  const IceServer *, const RTC::StunPacket* , Session *) {
}

// implements IceProber::Listener
void ConnectionFactory::Connection::OnIceProberBindingRequest() {
 ice_prober_->SendBindingRequest(ice_server_->GetSelectedSession());
}

// implements IceServer::Listener
void ConnectionFactory::Connection::OnDtlsTransportConnecting(const DtlsTransport* dtlsTransport) {
  TRACK();
}
void ConnectionFactory::Connection::OnDtlsTransportConnected(
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
void ConnectionFactory::Connection::OnDtlsTransportFailed(const DtlsTransport* dtlsTransport) {
  logger::info({{"ev","tls failed"}});
  OnDtlsTransportClosed(dtlsTransport);
}
// The DTLS connection has been closed due to receipt of a close_notify alert.
void ConnectionFactory::Connection::OnDtlsTransportClosed(const DtlsTransport* dtlsTransport) {
  logger::info({{"ev","tls closed"}});
  // Tell the parent class. (if we handle srtp, need to implement equivalent)
  // RTC::Transport::Disconnected();
  // above notifies TransportCongestionControlClient and TransportCongestionControlServer
  // may need to implement equivalent for performance
  factory().CloseConnection(*this); // this might be freed here, so don't touch after the line
}
// Need to send DTLS data to the peer.
void ConnectionFactory::Connection::OnDtlsTransportSendData(
  const DtlsTransport* dtlsTransport, const uint8_t* data, size_t len) {
  TRACK();
  auto *session = ice_server_->GetSelectedSession();
  if (session == nullptr) {
    logger::warn({{"proto","dtls"},{"ev","no selected tuple set, cannot send DTLS packet"}});
    return;
  }
  logger::info({{"ev","send dtls packet"},{"sz",len},{"to",session->addr().str()}});
  session->Send(reinterpret_cast<const char *>(data), len);
  // may need to implement equivalent
  // RTC::Transport::DataSent(len);
}
// DTLS application data received.
void ConnectionFactory::Connection::OnDtlsTransportApplicationDataReceived(
  const DtlsTransport*, const uint8_t* data, size_t len) {
  TRACK();
  sctp_association_->ProcessSctpData(data, len);
}

// implements SctpAssociation::Listener
void ConnectionFactory::Connection::OnSctpAssociationConnecting(SctpAssociation* sctpAssociation) {
  TRACK();
  // only notify
}
void ConnectionFactory::Connection::OnSctpAssociationConnected(SctpAssociation* sctpAssociation) {
  TRACK();
  sctp_connected_ = true;
}
void ConnectionFactory::Connection::OnSctpAssociationFailed(SctpAssociation* sctpAssociation) {
  TRACK();
  sctp_connected_ = false;
  // TODO: notify app
}
void ConnectionFactory::Connection::OnSctpAssociationClosed(SctpAssociation* sctpAssociation) {
  TRACK();
  sctp_connected_ = false;
  // TODO: notify app
}
void ConnectionFactory::Connection::OnSctpStreamReset(
  SctpAssociation* sctpAssociation, uint16_t streamId) {
  auto s = streams_.find(streamId);
  if (s == streams_.end()) {
    logger::error({{"proto","sctp"},{"ev","reset stream not found"},{"sid",streamId}});
    return;
  }
  s->second->Close(QRPC_CLOSE_REASON_REMOTE);
}
void ConnectionFactory::Connection::OnSctpAssociationSendData(
  SctpAssociation* sctpAssociation, const uint8_t* data, size_t len) {
  TRACK();
  if (!connected()) {
		logger::warn({{"proto","sctp"},{"ev","DTLS not connected, cannot send SCTP data"},
      {"dtls_state",dtls_transport_->GetState()}});
    return;
  }
  dtls_transport_->SendApplicationData(data, len);
}
void ConnectionFactory::Connection::OnSctpWebRtcDataChannelControlDataReceived(
  SctpAssociation* sctpAssociation,
  uint16_t streamId,
  const uint8_t* msg,
  size_t len) {
  // parse msg and create Stream::Config from it, then create stream by using NewStream
  TRACK();
  int r;
  switch (*msg) {
  case DATA_CHANNEL_ACK: {
    auto s = streams_.find(streamId);
    if (s == streams_.end()) {
      logger::error({{"proto","sctp"},{"ev","DATA_CHANNEL_ACK received for unknown stream"}});
      return;
    }
    if ((r = s->second->OnConnect()) < 0) {
      logger::error({{"proto","sctp"},{"ev","DATA_CHANNEL_ACK blocked for application reason"},{"rc",r}});
      s->second->Close(QRPC_CLOSE_REASON_LOCAL, r, "stream closed by application OnConnect");
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
    auto s = NewStream(c, factory().stream_factory());
    if (s == nullptr) {
      logger::error({{"proto","sctp"},{"ev","fail to create stream"},{"stream_id",streamId}});
      return;
    }
    // send dcep ack
    DcepResponse ack;
    uint8_t buff[ack.PayloadSize()];
    if ((r = sctpAssociation->SendSctpMessage(
        s.get(), PPID::WEBRTC_DCEP, ack.ToPaylod(buff, sizeof(buff)), ack.PayloadSize()
    )) < 0) {
      logger::error({{"proto","sctp"},{"ev","fail to send DCEP ACK"},{"stream_id",streamId},{"rc",r}});
      s->Close(QRPC_CLOSE_REASON_LOCAL, r, "fail to send DCEP ACK");
      return;
    }
  } break;
  }
}
void ConnectionFactory::Connection::OnSctpAssociationMessageReceived(
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
  int r;
  if ((r = it->second->OnRead(reinterpret_cast<const char *>(msg), len)) < 0) {
    logger::info({{"ev", "application close stream"},{"sid",streamId},{"rc",r}});
    it->second->Close(QRPC_CLOSE_REASON_LOCAL, r, "stream closed by application OnRead");
  }
}
void ConnectionFactory::Connection::OnSctpAssociationBufferedAmount(
  SctpAssociation* sctpAssociation, uint32_t len) {
  TRACK();
}

// client::WhipHttpProcessor, client::TcpSession, client::UdpSession
namespace client {
  typedef ConnectionFactory::IceUFlag IceUFlag;
  class WhipHttpProcessor : public HttpClient::Processor {
  public:
    WhipHttpProcessor(Client &c, const std::string &path) : client_(c), path_(), uflag_() {}
    ~WhipHttpProcessor() {}
  public:
    const std::string &path() const { return path_; }
    const IceUFlag &uflag() const { return uflag_; }
    void SetUFlag(std::string &&uflag) { uflag_ = std::move(IceUFlag(uflag)); }
  public:
    base::TcpSession *HandleResponse(HttpSession &s) override {
      const auto &uf = uflag();
      if (s.fsm().rc() != HRC_OK) {
        logger::error({{"ev","signaling server returns error response"},
          {"status",s.fsm().rc()},{"uflag",uf}});
        client_.CloseConnection(uf);
        return nullptr;
      }
      SDP sdp(s.fsm().body());
      auto candidates = sdp.Candidates();
      if (candidates.size() <= 0) {
        logger::error({{"ev","signaling server returns no candidates"},
          {"sdp",sdp},{"uflag",uf}});
        client_.CloseConnection(uf);
        return nullptr;
      }
      bool success = false;
      auto c = client_.FindFromUflag(uf);
      for (auto &cand : candidates) {
        if (!client_.Open(cand, c)) {
          logger::info({{"ev","fail to open"},{"cand",cand},{"uflag",uf}});
          continue;
        }
        success = true;
      }
      if (!success) {
        logger::info({{"ev","fail to open for all of candidates"},{"uflag",uf}});
        client_.CloseConnection(uf);
        return nullptr;
      }
      return nullptr;
    }
    int SendRequest(HttpSession &s) override {
      int r;
      std::string sdp, uflag;
      if ((r = client_.Offer(sdp, uflag)) < 0) {
        logger::error({{"ev","fail to generate offer"},{"rc",r}});
        return QRPC_ESYSCALL;
      }
      SetUFlag(std::move(uflag));
      std::string sdplen = std::to_string(sdp.length());
      HttpHeader h[] = {
          {.key = "Content-Type", .val = "application/sdp"},
          {.key = "Content-Length", .val = sdplen.c_str()}
      };
      return s.Request("POST", path().c_str(), h, 2, sdp.c_str(), sdp.length());
    }
  private:
    Client &client_;
    std::string path_;
    IceUFlag uflag_;
  };
  template <class BASE>
  class BaseSession : public BASE {
  public:
    typedef typename BASE::Factory Factory;
    BaseSession(Factory &f, Fd fd, const Address &addr, std::shared_ptr<Connection> &c,
      const std::string &remote_uflag, const std::string &remote_pwd) : 
      BASE(f, fd, addr, c), remote_uflag_(remote_uflag), remote_pwd_(remote_pwd),
      rctc_(qrpc_time_sec(1), qrpc_time_sec(30)) {}
    int OnConnect() override {
      rctc_.Connected();
      // start ICE prober.
      int r;
      if ((r = BASE::connection_->RunIceProber(this, remote_uflag_, remote_pwd_)) < 0) {
        logger::warn({{"ev","fail to start ICE prober"},{"rc",r}});
        return QRPC_EGOAWAY;
      }
      // after above, ICE server will receive stun success response at IceServer::ProcessStunPacket
      // and ConnectionFactory::Connection will be notified via OnIceServerSuccessResponded()
      return QRPC_OK;
    }
    qrpc_time_t OnShutdown() override {
      rctc_.Shutdown();
      return rctc_.Timeout();
    }
  private:
    std::string remote_uflag_, remote_pwd_;
    base::Session::ReconnectionTimeoutCalculator rctc_;
  };
  class TcpSession : public BaseSession<ConnectionFactory::TcpSession> {
  public:
    TcpSession(TcpSessionFactory &f, Fd fd, const Address &addr, std::shared_ptr<Connection> &c,
      const std::string &remote_uflag, const std::string &remote_pwd
    ) : BaseSession<ConnectionFactory::TcpSession>(f, fd, addr, c, remote_uflag, remote_pwd) {}
  };
  class UdpSession : public BaseSession<ConnectionFactory::UdpSession> {
  public:
    UdpSession(UdpSessionFactory &f, Fd fd, const Address &addr, std::shared_ptr<Connection> &c,
      const std::string &remote_uflag, const std::string &remote_pwd
    ) : BaseSession<ConnectionFactory::UdpSession>(f, fd, addr, c, remote_uflag, remote_pwd) {}
  };
}

// Client
bool Client::Open(
  Candidate &cand,
  std::shared_ptr<Connection> &c
) {
  std::string uflag = std::get<3>(cand);
  std::string pwd = std::get<4>(cand);
  if (std::get<0>(cand)) {
    if (!udp_ports_[0].Connect(
      std::get<1>(cand), std::get<2>(cand),
      [this, c, uflag, pwd](Fd fd, const Address a) mutable {
        return new client::UdpSession(udp_ports_[0], fd, a, c, uflag, pwd);
      }
    )) {
      logger::info({{"ev","fail to start UDP session"},
        {"to", std::get<1>(cand)},{"port",std::get<2>(cand)}});
      return false;
    }
  } else {
    if (!tcp_ports_[0].Connect(
      std::get<1>(cand), std::get<2>(cand),
      [this, c, uflag, pwd](Fd fd, const Address a) mutable {
        return new client::TcpSession(tcp_ports_[0], fd, a, c, uflag, pwd);
      }
    )) {
      logger::info({{"ev","fail to start TCP session"},
        {"to", std::get<1>(cand)},{"port",std::get<2>(cand)}});
      return false;
    }
  }
  return true;
}
int Client::Offer(std::string &sdp, std::string &uflag) {
  logger::info({{"ev","new client connection"}});
  // server connection's dtls role is client, workaround fo osx safari (16.4) does not initiate DTLS handshake
  // even if sdp anwser ask to do it.
  auto c = std::shared_ptr<Connection>(new Connection(*this, DtlsTransport::Role::SERVER));
  if (c == nullptr) {
    logger::error({{"ev","fail to allocate connection"}});
    return QRPC_EALLOC;
  }
  int r;
  std::string pwd;
  if ((r = c->Init(uflag, pwd)) < 0) {
    logger::error({{"ev","fail to init connection"},{"rc",r}});
    return QRPC_EINVAL;
  }
  if ((r = SDP::Offer(*c, uflag, pwd, sdp)) < 0) {
    logger::error({{"ev","fail to create offer"},{"rc",r}});
    return QRPC_EINVAL;
  }
  connections_.emplace(std::move(uflag), c);
  return false;
}
bool Client::Connect(const std::string &host, int port, const std::string &path) {
  return http_client_.Connect(host, port, new client::WhipHttpProcessor(*this, path));
}

// Server
bool Server::Listen(
  int signaling_port, int port,
  const std::string &listen_ip, const std::string &path
) {
  int r;
  if (signaling_port <= 0) {
    DIE("signaling port must be positive");
  }
  config_.ports = {
    {.protocol = ConnectionFactory::Port::UDP, .port = port},
    {.protocol = ConnectionFactory::Port::TCP, .port = port}
  };
  if ((r = Init()) < 0) {
    logger::error({{"ev","fail to init server"},{"rc",r}});
    return false;
  }
  router_.Route(std::regex(path), [this](HttpSession &s, std::cmatch &) {
    int r;
    std::string sdp;
    if ((r = Accept(s.fsm().body(), sdp)) < 0) {
        logger::error("fail to create connection");
        s.ServerError("server error %d", r);
    }
    std::string sdplen = std::to_string(sdp.length());
    HttpHeader h[] = {
        {.key = "Content-Type", .val = "application/sdp"},
        {.key = "Content-Length", .val = sdplen.c_str()}
    };
    s.Respond(HRC_OK, h, 2, sdp.c_str(), sdp.length());
    return nullptr;
  });
  if (!http_listener_.Listen(signaling_port, router_)) {
    logger::error({{"ev","fail to listen on signaling port"},{"port",signaling_port}});
    return false;
  }
  return true;
}
int Server::Accept(const std::string &client_sdp, std::string &server_sdp) {
  logger::info({{"ev","new server connection"},{"client_sdp", client_sdp}});
  // server connection's dtls role is client, workaround fo osx safari (16.4) does not initiate DTLS handshake
  // even if sdp anwser ask to do it.
  auto c = std::shared_ptr<Connection>(new Connection(*this, DtlsTransport::Role::CLIENT));
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
} //namespace webrtc
} //namespace base