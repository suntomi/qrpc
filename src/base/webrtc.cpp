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
  if ((r = GlobalInit(alarm_processor())) < 0) {
    return r;
  }
  if ((r = config_.Derive()) < 0) {
    return r;
  }
  if ((r = Setup())) {
    return r;
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
  connections_.clear();
  if (alarm_id_ != AlarmProcessor::INVALID_ID) {
    alarm_processor().Cancel(alarm_id_);
    alarm_id_ = AlarmProcessor::INVALID_ID;
  }
  GlobalFin();
}
void ConnectionFactory::CloseConnection(Connection &c) {
  logger::info({{"ev","close webrtc connection"},{"ufrag",c.ufrag()}});
  c.Fin(); // cleanup resources if not yet
  connections_.erase(c.ufrag());
  // c might be freed here
}
static inline ConnectionFactory::IceUFrag GetLocalIceUFragFrom(RTC::StunPacket* packet) {
  TRACK();

  // Here we inspect the USERNAME attribute of a received STUN request and
  // extract its remote usernameFragment (the one given to our IceServer as
  // local usernameFragment) which is the first value in the attribute value
  // before the ":" symbol.

  const auto& username  = packet->GetUsername();
  const size_t colonPos = username.find(':');

  // If no colon is found just return the whole USERNAME attribute anyway.
  if (colonPos == std::string::npos) {
    return username;
  }

  return username.substr(0, colonPos);
}
std::shared_ptr<ConnectionFactory::Connection>
ConnectionFactory::FindFromStunRequest(const uint8_t *p, size_t sz) {
  RTC::StunPacket* packet = RTC::StunPacket::Parse(p, sz);
  if (packet == nullptr) {
    QRPC_LOG(warn, "ignoring wrong STUN packet received");
    return nullptr;
  }
  QRPC_LOGJ(info, {{"ev","STUN packet received"},{"username",packet->GetUsername()}})
  // try to match the local ICE username fragment.
  auto key = GetLocalIceUFragFrom(packet);
  // stun binding response from server does not contains username fragment
  // usually client session created with Connection object, no FindFromStunRequest call needed.
  // but when client give up one endpoint in SDP and using next one, packet from old endpoint might be received.
  // in that case, session for that endoint will be created again without provided connection object,
  // control flow will reach to here. so for client, we ignore that error
  ASSERT(!key.empty() || is_client());
  auto it = connections_.find(key);
  delete packet;

  if (it == this->connections_.end()) {
    logger::warn({
      {"ev","ignoring received STUN packet with unknown remote ICE usernameFragment"},
      {"ufrag",key}
    });
    return nullptr;
  }
  return it->second;
}
std::shared_ptr<ConnectionFactory::Connection>
ConnectionFactory::FindFromUfrag(const IceUFrag &ufrag) {
    auto it = connections_.find(ufrag);
    if (it == this->connections_.end()) {
      return nullptr;
    }
    return it->second;
}
uint32_t ConnectionFactory::g_ref_count_ = 0;
std::mutex ConnectionFactory::g_ref_sync_mutex_;
int ConnectionFactory::GlobalInit(AlarmProcessor &a) {
	try
	{
    std::lock_guard<std::mutex> lock(g_ref_sync_mutex_);
    if (g_ref_count_ == 0) {
      // Initialize static stuff.
      DepOpenSSL::ClassInit();
      DepLibSRTP::ClassInit();
      DepUsrSCTP::ClassInit(a);
      DepLibWebRTC::ClassInit();
      Utils::Crypto::ClassInit();
      DtlsTransport::ClassInit();
      RTC::SrtpSession::ClassInit();
    }
    g_ref_count_++;
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
    std::lock_guard<std::mutex> lock(g_ref_sync_mutex_);
    g_ref_count_--;
    if (g_ref_count_ > 1) {
      return;
    }
    // Free static stuff.
		DtlsTransport::ClassDestroy();
		Utils::Crypto::ClassDestroy();
		DepLibWebRTC::ClassDestroy();
		DepUsrSCTP::ClassDestroy();
		DepLibSRTP::ClassDestroy();
  }
  catch (const MediaSoupError& error) {
    logger::error({{"ev","mediasoup cleanup failure"},{"reason",error.what()}});
	}
}

// ConnectionFactory::Config
int ConnectionFactory::Config::Derive() {
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
    connection_ = connection_factory().FindFromStunRequest(up, sz);
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
    connection_ = connection_factory().FindFromStunRequest(up, sz);
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

/* ConnectionFactory::SyscallStream */
int ConnectionFactory::SyscallStream::OnRead(const char *p, size_t sz) {
  auto pl = std::string(p, sz);
  try {
    auto data = json::parse(pl);
    auto fnit = data.find("fn");
    if (fnit == data.end()) {
      QRPC_LOGJ(info, {{"ev", "syscall invalid payload"},{"r", "no 'fn' key"},{"pl",pl}});
      return QRPC_OK;
    }
    const auto &fn = fnit->get<std::string>();
    auto &c = dynamic_cast<Connection &>(connection());
    QRPC_LOGJ(info, {{"ev", "recv from syscall stream"},{"fn",fn}});
    if (fn == "close") {
      QRPC_LOGJ(info, {{"ev", "shutdown from peer"}});
      c.factory().ScheduleClose(c);
    } else if (fn == "nego") {
      const auto ait = data.find("args");
      if (ait == data.end()) {
        QRPC_LOGJ(error, {{"ev","syscall invalid payload"},{"fn",fn},{"pl",pl},{"r","no value for key 'args'"}});
        return QRPC_OK;
      }
      const auto &args = ait->get<std::map<std::string,json>>();
      const auto sit = args.find("sdp");
      if (sit == args.end()) {
        QRPC_LOGJ(error, {{"ev","syscall invalid payload"},{"fn",fn},{"pl",pl},{"r","no value for key 'sdp'"}});
        return QRPC_OK;
      }
      const auto git = args.find("gen");
      if (git == args.end()) {
        QRPC_LOGJ(error, {{"ev","syscall invalid payload"},{"fn",fn},{"pl",pl},{"r","no value for key 'gen'"}});
        return QRPC_OK;
      }
      const auto lit = args.find("label");
      if (lit == args.end()) {
        QRPC_LOGJ(error, {{"ev","syscall invalid payload"},{"fn",fn},{"pl",pl},{"r","no value for key 'label'"}});
        return QRPC_OK;
      }
      const auto rlmit = args.find("ridLabelMap");
      if (rlmit != args.end()) {
        c.rid_label_map_ = rlmit->second.get<std::map<std::string,std::string>>();
        QRPC_LOGJ(info, {{"ev","new rid label map"},{"map",*rlmit}});
      }
      const auto &sdp_text = sit->second.get<std::string>();
      std::string answer;
      SDP sdp(sdp_text);
      if (!sdp.Answer(c, answer)) {
        QRPC_LOGJ(error, {{"ev","invalid client sdp"},{"sdp",sdp_text},{"reason",answer}});
        Call("nego_ack",{{"gen",git->second.get<uint64_t>()},{"error",answer}});
        return QRPC_OK;
      }
      Call("nego_ack",{{"gen",git->second.get<uint64_t>()},{"sdp",answer},{"label",lit->second.get<std::string>()}});
    } else {
      QRPC_LOGJ(error, {{"ev","syscall is not supported"},{"fn",fn}});
      ASSERT(false);
      return QRPC_OK;
    }
  }
  catch (const std::exception& error) {
    QRPC_LOGJ(error, {{"ev","json parse error"},{"err",error.what()}})
  }
  return QRPC_OK;
}
int ConnectionFactory::SyscallStream::Call(const char *fn) {
  return Send({{"fn",fn}});
}
int ConnectionFactory::SyscallStream::Call(const char *fn, const json &j) {
  return Send({{"fn",fn},{"args",j}});
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
int ConnectionFactory::Connection::Init(std::string &ufrag, std::string &pwd) {
  if (ice_server_ != nullptr) {
    logger::warn({{"ev","already init"}});
    return QRPC_OK;
  }
  ufrag = random::word(32);
  pwd = random::word(32);
  // create ICE server
  ice_server_.reset(new IceServer(this, ufrag, pwd));
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
    logger::error({{"ev","stream id already used"},{"sid",c.params.streamId}});
    ASSERT(false);
    return nullptr;
  }
  auto s = sf(c, *this);
  if (s == nullptr) {
    logger::error({{"ev","fail to create stream"},{"sid",c.params.streamId}});
    ASSERT(false);
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
  if (s == nullptr) {
    QRPC_LOGJ(error, {{"ev","fail to create stream"},{"sid",c.params.streamId},{"l",c.label}});
    ASSERT(false);
    return nullptr;
  }
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
  if (ice_prober_ != nullptr) {
    ice_prober_->Reset();
  }
  for (auto s = streams_.begin(); s != streams_.end();) {
    auto cur = s++;
    (*cur).second->OnShutdown();
  }
  OnFinalize();
  streams_.clear();
  if (ice_server_ != nullptr) {
    for (auto it = ice_server_->GetSessions().begin(); it != ice_server_->GetSessions().end();) {
      auto s = it++;
      (*s)->Close(QRPC_CLOSE_REASON_SHUTDOWN, 0, "parent webrtc connection closed");
    }
  }
}
void ConnectionFactory::Connection::OnFinalize() {
  auto reconnect_wait = OnShutdown();
  if (factory().is_client()) {
    auto &c = factory().to<Client>();
    auto &uf = ufrag();
    if (reconnect_wait > 0) {
      QRPC_LOGJ(info, {{"ev","start reconnect wait"},{"backoff",reconnect_wait},{"ufrag",uf}})
      c.alarm_processor().Set([&c, uf = uf]() {
        auto epit = c.endpoints().find(uf);
        if (epit != c.endpoints().end()) {
          auto &ep = (*epit).second;
          QRPC_LOGJ(info, {{"ev","reconnection start"},{"uf",uf},
            {"ep",(ep.host + ":" + std::to_string(ep.port) + ep.path)}});
          c.Connect(ep.host, ep.port, ep.path);
          c.endpoints().erase(epit);
        } else {
          QRPC_LOGJ(warn, {{"ev","reconnection cancel"},{"r","endpoint not found"},{"uf",uf}});
          ASSERT(false);
        }
        return 0;
      }, qrpc_time_now() + reconnect_wait);
    } else {
      QRPC_LOGJ(info, {{"ev","stop reconnection"},{"ufrag",uf}})
      c.endpoints().erase(uf);
    }
  }
}
void ConnectionFactory::Connection::Close() {
  if (closed()) {
    return;
  }
  if (syscall_ == nullptr) {
    syscall_ = std::dynamic_pointer_cast<SyscallStream>(OpenStream({
      .label = SyscallStream::NAME
    }, [this](const Stream::Config &config, base::Connection &conn) {
      return std::make_shared<SyscallStream>(conn, config, [this](Stream &s) {
        this->closed_ = true;
        QRPC_LOGJ(info, {{"ev","server syscall stream opened"},{"sid",s.id()}});
        return s.Send({{"fn","close"}});
      });
    }));
  } else {
    syscall_->Call("close");
  }
}
IceProber *ConnectionFactory::Connection::InitIceProber(
  const std::string &ufrag, const std::string &pwd, uint64_t priority) {
  TRACK();
  if (!ice_prober_) {
    ice_prober_ = std::make_unique<IceProber>(ufrag, pwd, priority);
  }
  return ice_prober_.get();
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
    factory().ScheduleClose(*this);
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
void ConnectionFactory::Connection::TryParseRtcpPacket(const uint8_t *p, size_t sz) {
  // Decrypt the SRTCP packet.
  auto intLen = static_cast<int>(sz);
  auto decrypted = srtp_recv_->DecryptSrtcp(const_cast<uint8_t *>(p), &intLen);
  if (!decrypted) {
    QRPC_LOGJ(warn, {{"proto","srtcp"},
      {"ev","received data is not a valid RTP packet"},
      {"decrypted",decrypted},{"intLen",intLen},
      {"pl",str::HexDump(p, std::min((size_t)32, sz))}});
    ASSERT(false);
    return;
  }
  RTC::RTCP::Packet* packet = RTC::RTCP::Packet::Parse(p, static_cast<size_t>(intLen));
  if (!packet) {
    logger::warn({{"proto","srtcp"},
      {"ev","received data is not a valid RTCP compound or single packet"},
      {"pl",str::HexDump(p, std::min((size_t)32, sz))}});
    return;
  }
  // we need to implement RTC::Transport::ReceiveRtcpPacket(packet) equivalent
  logger::info({{"ev","RTCP packet received"}});
  packet->Dump();
  delete packet;
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
  TryParseRtcpPacket(p, sz);
  return QRPC_OK;
}
void ConnectionFactory::Connection::TryParseRtpPacket(const uint8_t *p, size_t sz) {
  // Decrypt the SRTP packet.
  auto intLen = static_cast<int>(sz);
  auto decrypted = this->srtp_recv_->DecryptSrtp(const_cast<uint8_t*>(p), &intLen);
  auto *packet = RTC::RtpPacket::Parse(p, static_cast<size_t>(intLen));
  if (packet == nullptr) {
    QRPC_LOGJ(warn, {{"proto","rtcp"},
      {"ev","received data is not a valid RTP packet"},
      {"decrypted",decrypted},{"intLen",intLen},
      {"pl",str::HexDump(p, std::min((size_t)32, sz))}});
    ASSERT(false);
    return;
  }
  std::string rid = "none", mid = "none";;
  packet->ReadRid(rid);
  packet->ReadMid(mid);
  if (!decrypted) {
    QRPC_LOGJ(warn, {
      {"ev","RTP packet received, but decryption fails"},
      {"proto","srtp"},{"ssrc",packet->GetSsrc()},{"rid",rid},{"mid",mid},
      {"payloadType",packet->GetPayloadType()},{"seq",packet->GetSequenceNumber()}
    });
  } else {
    QRPC_LOGJ(debug, {
      {"ev","RTP packet received"},
      {"proto","srtp"},{"ssrc",packet->GetSsrc()},{"rid",rid},{"mid",mid},
      {"payloadType",packet->GetPayloadType()},{"seq",packet->GetSequenceNumber()}
    });
  }
  delete packet;
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
  // parse
  TryParseRtpPacket(p, sz);
  // Trick for clients performing aggressive ICE regardless we are ICE-Lite.
  this->ice_server_->MayForceSelectedSession(session);
  // we need to implement RTC::Transport::ReceiveRtpPacket(packet); equivalent
  return QRPC_OK;
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
  if (!s.reset()) {
    // client: even is outgoing, odd is incoming
    // server: even is incoming, odd is outgoing
    // bool isOutgoing = (dtls_role_ == DtlsTransport::Role::CLIENT) == ((s.id() % 2) == 0);
    sctp_association_->DataConsumerClosed(&s);
    s.SetReset();
  } else {
    QRPC_LOGJ(debug, {{"ev","do not send reset because already reset"},{"stream",s.id()}});
  }
  // stream removed after Connection::OnSctpStreamReset called
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
  logger::info({{"ev","OnIceServerLocalUsernameFragmentAdded"},{"ufrag",usernameFragment}});
  // mediasoup seems to add Connection itself to ConnectionFactory's map here.
  // and OnIceServerLocalUsernameFragmentAdded is called from WebRtcTransport::ctor
  // thus, if mediasoup creates WebRtcTransport, it will be added to the map automatically
  // but it is too implicit. I rather prefer to add it manualy, in NewConnection
}
void ConnectionFactory::Connection::OnIceServerLocalUsernameFragmentRemoved(
  const IceServer *iceServer, const std::string& usernameFragment) {
  logger::info({{"ev","OnIceServerLocalUsernameFragmentRemoved"},{"c",str::dptr(this)},{"ufrag",usernameFragment}});
  sv_.ScheduleClose(usernameFragment);
}
void ConnectionFactory::Connection::OnIceServerSessionAdded(const IceServer *iceServer, Session *session) {
  logger::info({{"ev","OnIceServerSessionAdded"},{"ss",str::dptr(session)}});
  // used for synching server's session/address map. 
  // use OnIceServerTupleAdded to search mediasoup's example
}
void ConnectionFactory::Connection::OnIceServerSessionRemoved(
  const IceServer *iceServer, Session *session) {
  logger::info({{"ev","OnIceServerSessionRemoved"},{"ss",str::dptr(session)}});
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
    factory().ScheduleClose(*this);
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
  if (!ice_prober_ || dtls_role_ == DtlsTransport::Role::CLIENT) {
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
  factory().ScheduleClose(*this); // this might be freed here, so don't touch after the line
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
  // logger::info({{"ev","send dtls packet"},{"sz",len},{"to",session->addr().str()}});
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
  s->second->OnShutdown();
  streams_.erase(s);
}
void ConnectionFactory::Connection::OnSctpAssociationSendData(
  SctpAssociation* sctpAssociation, const uint8_t* data, size_t len) {
  TRACK();
  if (!connected()) {
		logger::warn({{"proto","sctp"},{"ev","DTLS not connected, cannot send SCTP data"},
      {"dtls_state",dtls_transport_->GetState()}});
    return;
  }
  logger::debug({{"proto","stcp"},{"ev","send data"},{"sz",len}});
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
    QRPC_LOGJ(info, {{"ev","DATA_CHANNEL_ACK received"},{"sid",streamId}});
    auto s = streams_.find(streamId);
    if (s == streams_.end()) {
      logger::error({{"proto","sctp"},{"ev","DATA_CHANNEL_ACK received for unknown stream"},{"sid",streamId}});
      ASSERT(false);
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
    QRPC_LOGJ(info, {{"ev","DATA_CHANNEL_OPEN received"},{"sid",streamId}});
    if (req == nullptr) {
      logger::error({{"proto","sctp"},{"ev","invalid DCEP request received"}});
      return;
    }
    auto c = req->ToStreamConfig();
    auto s = NewStream(
      c, c.label == SyscallStream::NAME ? 
        [this](const Stream::Config &config, base::Connection &conn) {
          return this->syscall_ = std::make_shared<SyscallStream>(conn, config);
        } : factory().stream_factory()
    );
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
    QRPC_LOGJ(info, {{"ev","DATA_CHANNEL_ACK sent"},{"sid",streamId}});
    // because OnConnect may send packet, it should be done after ack, 
    // to assure stream open callback called before first stream read callback
    if ((r = s->OnConnect()) < 0) {
      logger::error({{"proto","sctp"},{"ev","DATA_CHANNEL_OPEN blocked for application reason"},{"rc",r}});
      s->Close(QRPC_CLOSE_REASON_LOCAL, r, "stream closed by application OnConnect");
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
  typedef ConnectionFactory::IceUFrag IceUFrag;
  class WhipHttpProcessor : public HttpClient::Processor {
  public:
    WhipHttpProcessor(Client &c, const Client::Endpoint &ep) :
      client_(c), ufrag_(), ep_(ep) {}
    ~WhipHttpProcessor() {}
  public:
    const IceUFrag &ufrag() const { return ufrag_; }
    const Client::Endpoint &endpoint() const { return ep_; }
    void SetUFrag(std::string &&ufrag) { ufrag_ = ufrag; }
  public:
    base::TcpSession *HandleResponse(HttpSession &s) override {
      // in here, session that related with webrtc connection is not actively callbacked, 
      // so we can call CloseConnection
      const auto &uf = ufrag();
      if (s.fsm().rc() != HRC_OK) {
        logger::error({{"ev","signaling server returns error response"},
          {"status",s.fsm().rc()},{"ufrag",uf}});
        client_.ScheduleClose(uf);
        return nullptr;
      }
      SDP sdp(s.fsm().body());
      auto c = client_.FindFromUfrag(uf);
      if (c == nullptr) {
        // may timeout
        logger::error({{"ev","connection not found"},{"ufrag",uf}});
        client_.ScheduleClose(uf);
        return nullptr;
      }
      auto candidates = sdp.Candidates();
      if (candidates.size() <= 0) {
        logger::error({{"ev","signaling server returns no candidates"},
          {"sdp",sdp},{"ufrag",uf}});
        client_.ScheduleClose(uf);
        return nullptr;
      }
      if (!client_.Open(candidates, 0, c)) {
        client_.ScheduleClose(uf);
      }
      return nullptr;
    }
    int SendRequest(HttpSession &s) override {
      int r;
      std::string sdp, ufrag;
      if ((r = client_.Offer(ep_, sdp, ufrag)) < 0) {
        QRPC_LOGJ(error, {{"ev","fail to generate offer"},{"rc",r}});
        return QRPC_ESYSCALL;
      }
      SetUFrag(std::move(ufrag));
      std::string sdplen = std::to_string(sdp.length());
      HttpHeader h[] = {
          {.key = "Content-Type", .val = "application/sdp"},
          {.key = "Content-Length", .val = sdplen.c_str()}
      };
      return s.Request("POST", ep_.path.c_str(), h, 2, sdp.c_str(), sdp.length());
    }
    void HandleClose(HttpSession &, const CloseReason &r) override {
      // in here, session that related with webrtc connection is not actively callbacked, 
      // so we can call CloseConnection
      if (r.code != QRPC_CLOSE_REASON_LOCAL || r.detail_code != QRPC_EGOAWAY) {
        QRPC_LOGJ(info, {{"ev","close webrtc connection by whip failure"},{"rc",r.code},{"dc",r.detail_code}});
        client_.ScheduleClose(ufrag_);
      }
    }
  private:
    Client &client_;
    IceUFrag ufrag_;
    Client::Endpoint ep_;
  };
  typedef std::function<void (int)> OnIceFailure;
  template <class BASE>
  class BaseSession : public BASE {
  public:
    typedef typename BASE::Factory Factory;
    BaseSession(Factory &f, Fd fd, const Address &addr, std::shared_ptr<Connection> &c,
      const std::string &remote_uflag, const std::string &remote_pwd, uint64_t priority,
      OnIceFailure &&on_ice_failure) : 
      BASE(f, fd, addr, c), remote_ufrag_(remote_uflag), remote_pwd_(remote_pwd), priority_(priority),
      prober_(nullptr), on_ice_failure_(std::move(on_ice_failure)), rctc_(qrpc_time_sec(1), qrpc_time_sec(30)) {}
    int OnConnect() override {
      rctc_.Connected();
      // start ICE prober.
      prober_ = BASE::connection_->InitIceProber(remote_ufrag_, remote_pwd_, priority_);
      if (alarm_id_ == AlarmProcessor::INVALID_ID) {
        alarm_id_ = BASE::factory().alarm_processor().Set([this]() { return this->operator()(); }, qrpc_time_now());
      } else {
        ASSERT(false);
      }
      logger::info({{"ev","session connected"},{"proto",BASE::proto()},{"tid",alarm_id_}});
      // after above, ICE server will receive stun success response at IceServer::ProcessStunPacket
      // and ConnectionFactory::Connection will be notified via OnIceServerSuccessResponded()
      return QRPC_OK;
    }
    qrpc_time_t OnShutdown() override {
      logger::info({{"ev","session shutdown"},{"proto",BASE::proto()},{"tid",alarm_id_}});
      if (alarm_id_ != AlarmProcessor::INVALID_ID) {
        BASE::factory().alarm_processor().Cancel(alarm_id_);
        alarm_id_ = AlarmProcessor::INVALID_ID;
      }
      // removes Tuple(this session) from IceServer
      BASE::OnShutdown();
      if (BASE::close_reason().code == QRPC_CLOSE_REASON_TIMEOUT) {
        on_ice_failure_(QRPC_CLOSE_REASON_TIMEOUT);
        return 0ULL; // stop reconnection;
      }
      rctc_.Shutdown();
      return rctc_.Timeout();
    }
    qrpc_time_t operator()() {
      ASSERT(prober_ != nullptr);
      auto next = prober_->OnTimer(this);
      if (next == 0ULL) {
        // prevent OnShutdown from canceling alarm
        alarm_id_ = AlarmProcessor::INVALID_ID;
        BASE::Close(QRPC_CLOSE_REASON_TIMEOUT, 0, "wait ice prober connected");
      }
      return next;
    }
  private:
    std::string remote_ufrag_, remote_pwd_;
    uint64_t priority_;
    IceProber *prober_;
    OnIceFailure on_ice_failure_;
    AlarmProcessor::Id alarm_id_{AlarmProcessor::INVALID_ID};
    base::Session::ReconnectionTimeoutCalculator rctc_;
  };
  class TcpSession : public BaseSession<Client::TcpSession> {
  public:
    TcpSession(Factory &f, Fd fd, const Address &addr, std::shared_ptr<Connection> &c,
      const Candidate &cand, OnIceFailure &&oif
    ) : BaseSession<Client::TcpSession>(
      f, fd, addr, c, std::get<3>(cand), std::get<4>(cand), std::get<5>(cand), std::move(oif)
    ) {}
  };
  class UdpSession : public BaseSession<Client::UdpSession> {
  public:
    UdpSession(Factory &f, Fd fd, const Address &addr, std::shared_ptr<Connection> &c,
      const Candidate &cand, OnIceFailure &&oif
    ) : BaseSession<Client::UdpSession>(
      f, fd, addr, c, std::get<3>(cand), std::get<4>(cand), std::get<5>(cand), std::move(oif)
    ) {}
  };
}

// Client
bool Client::Open(
  const std::vector<Candidate> &candidates,
  size_t idx,
  std::shared_ptr<Connection> &c
) {
  if (candidates.size() <= idx) {
    logger::info({{"ev","no more candidate to open"},
      {"candidates_size", candidates.size()}});
    return false;
  }
  const auto &cand = candidates[idx];
  std::string ufrag = std::get<3>(cand);
  std::string pwd = std::get<4>(cand);
  auto on_failure = [this, candidates, idx, c, ufrag](int status) mutable {
    // try next candidate
    if (!this->Open(candidates, idx + 1, c)) {
      this->CloseConnection(ufrag);
    }
  };
  // set remote finger print
  c->dtls_transport().SetRemoteFingerprint(std::get<6>(cand));
  if (std::get<0>(cand)) {
    if (!udp_clients_[0].Connect(
      std::get<1>(cand), std::get<2>(cand),
      [this, c, cand, on_failure](Fd fd, const Address a) mutable {
        return new client::UdpSession(udp_clients_[0], fd, a, c, cand, std::move(on_failure));
      }, on_failure
    )) {
      logger::info({{"ev","fail to start session"},{"proto","udp"},
        {"to",std::get<1>(cand)},{"port",std::get<2>(cand)}});
      return false;
    }
  } else {
    if (!tcp_clients_[0].Connect(
      std::get<1>(cand), std::get<2>(cand),
      [this, c, cand, on_failure](Fd fd, const Address a) mutable {
        return new client::TcpSession(tcp_clients_[0], fd, a, c, cand, std::move(on_failure));
      }, on_failure
    )) {
      logger::info({{"ev","fail to start session"},{"proto","tcp"},
        {"to",std::get<1>(cand)},{"port",std::get<2>(cand)}});
      return false;
    }
  }
  return true;
}
int Client::Offer(const Endpoint &ep, std::string &sdp, std::string &ufrag) {
  logger::info({{"ev","new client connection"}});
  // client connection's dtls role is server, workaround fo osx safari (16.4) does not initiate DTLS handshake
  // even if sdp anwser ask to do it.
  auto c = std::shared_ptr<Connection>(factory_method_(*this, DtlsTransport::Role::SERVER));
  if (c == nullptr) {
    logger::error({{"ev","fail to allocate connection"}});
    return QRPC_EALLOC;
  }
  int r;
  std::string pwd;
  if ((r = c->Init(ufrag, pwd)) < 0) {
    logger::error({{"ev","fail to init connection"},{"rc",r}});
    return QRPC_EINVAL;
  }
  if ((r = SDP::Offer(*c, ufrag, pwd, sdp)) < 0) {
    logger::error({{"ev","fail to create offer"},{"rc",r}});
    return QRPC_EINVAL;
  }
  endpoints_.emplace(ufrag, ep);
  connections_.emplace(ufrag, c);
  return QRPC_OK;
}
bool Client::Connect(const std::string &host, int port, const std::string &path) {
  if (udp_clients_.size() <= 0 && tcp_clients_.size() <= 0) {
    // init client
    config_.ports = {
      // 0 for local port number auto assignment
      {.protocol = ConnectionFactory::Port::UDP, .port = 0},
      {.protocol = ConnectionFactory::Port::TCP, .port = 0}
    };
    int r;
    if ((r = Init()) < 0) {
      QRPC_LOGJ(error, {{"ev","fail to init conection factory"},{"rc",r}});
      return r;
    }
  }

  QRPC_LOGJ(info, {{"ev","connect start"},
    {"ep",(host + ":" + std::to_string(port) + path)}});
  
  return http_client_.Connect(host, port, new client::WhipHttpProcessor(*this, {
    .host = host, .port = port, .path = path
  }));
}
int Client::Setup() {
  // setup TCP/UDP ports
  for (auto port : config_.ports) {
    switch (port.protocol) {
      case Port::Protocol::UDP:
        udp_clients_.emplace_back(*this);
        break;
      case Port::Protocol::TCP:
        tcp_clients_.emplace_back(*this);
        break;
      default:
        logger::error({{"ev","unsupported protocol"},{"proto",port.protocol}});
        return QRPC_ENOTSUPPORT;
    }
  }
  return QRPC_OK;
}
void Client::Fin() {
  for (auto it = udp_clients_.begin(); it != udp_clients_.end();) {
    auto p = it++;
    (*p).Fin();
  }
  for (auto it = tcp_clients_.begin(); it != tcp_clients_.end();) {
    auto p = it++;
    (*p).Fin();
  }
}


// Listener::TcpSession
ConnectionFactory &Listener::TcpSession::connection_factory() {
  return factory().to<TcpPort>().connection_factory();
}
// Listener::UdpSession
ConnectionFactory &Listener::UdpSession::connection_factory() {
  return factory().to<UdpPort>().connection_factory();
}
// Listener
bool Listener::Listen(
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
        return nullptr;
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
int Listener::Accept(const std::string &client_req_body, std::string &server_sdp) {
  try {
    auto client_req = json::parse(client_req_body);
    logger::info({{"ev","new server connection"},{"client_req", client_req}});
    auto client_sdp = client_req["sdp"].get<std::string>();
    // server connection's dtls role is client, workaround fo osx safari (16.4) does not initiate DTLS handshake
    // even if sdp anwser ask to do it.
    auto c = std::shared_ptr<Connection>(factory_method_(*this, DtlsTransport::Role::CLIENT));
    if (c == nullptr) {
      logger::error({{"ev","fail to allocate connection"}});
      return QRPC_EALLOC;
    }
    int r;
    std::string ufrag, pwd;
    if ((r = c->Init(ufrag, pwd)) < 0) {
      logger::error({{"ev","fail to init connection"},{"rc",r}});
      return QRPC_EINVAL;
    }
    SDP sdp(client_sdp);
    if (!sdp.Answer(*c, server_sdp)) {
      logger::error({{"ev","invalid client sdp"},{"sdp",client_sdp},{"reason",server_sdp}});
      return QRPC_EINVAL;
    }
    connections_.emplace(std::move(ufrag), c);
  } catch (const std::exception &e) {
    QRPC_LOGJ(error, {{"ev","malform request"},{"reason",e.what()},{"req",client_req_body}});
    return QRPC_EINVAL;
  }
  return QRPC_OK;
}
int Listener::Setup() {
  // setup TCP/UDP ports
  for (auto port : config_.ports) {
    switch (port.protocol) {
      case Port::Protocol::UDP: {
        auto &p = udp_ports_.emplace_back(*this);
      if (!p.Listen(port.port)) {
          logger::error({{"ev","fail to listen"},{"port",port.port}});
          return QRPC_ESYSCALL;
        }
      } break;
      case Port::Protocol::TCP: {
        auto &p = tcp_ports_.emplace_back(*this);
        if (!p.Listen(port.port)) {
          logger::error({{"ev","fail to listen"},{"port",port.port}});
          return QRPC_ESYSCALL;
        }
      } break;
      default:
        logger::error({{"ev","unsupported protocol"},{"proto",port.protocol}});
        return QRPC_ENOTSUPPORT;
    }
  }
  return QRPC_OK;
}
void Listener::Fin() {
  for (auto it = udp_ports_.begin(); it != udp_ports_.end();) {
    auto p = it++;
    (*p).Fin();
  }
  for (auto it = tcp_ports_.begin(); it != tcp_ports_.end();) {
    auto p = it++;
    (*p).Fin();
  }
}
} //namespace webrtc
} //namespace base