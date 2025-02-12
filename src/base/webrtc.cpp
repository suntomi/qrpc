#include "base/defs.h"
#include "base/crypto.h"
#include "base/webrtc.h"
#include "base/webrtc/sctp.h"
#include "base/webrtc/dcep.h"
#include "base/webrtc/sdp.h"

#include "common.hpp"
#include "handles/TimerHandle.hpp"
#include "handles/UnixStreamSocketHandle.hpp"
#include "MediaSoupErrors.hpp"
#include "DepLibSRTP.hpp"
#include "DepLibUV.hpp"
#include "DepLibWebRTC.hpp"
#include "DepUsrSCTP.hpp"
#include "DepOpenSSL.hpp"
#include "FBS/message.h"
#include "Logger.hpp"
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
  logger::info({{"ev","close webrtc connection"},{"ufrag",c.ufrag()},{"cname",c.cname()}});
  c.Fin(); // cleanup resources if not yet but c itself does not freed
  cnmap_.erase(c.cname());
  connections_.erase(c.ufrag());
  // c might be freed here (if all reference from shared_ptr is released)
}
static inline ConnectionFactory::IceUFrag GetLocalIceUFragFrom(RTC::StunPacket& packet) {
  TRACK();

  // Here we inspect the USERNAME attribute of a received STUN request and
  // extract its remote usernameFragment (the one given to our IceServer as
  // local usernameFragment) which is the first value in the attribute value
  // before the ":" symbol.

  const auto& username  = packet.GetUsername();
  const size_t colonPos = username.find(':');

  // If no colon is found just return the whole USERNAME attribute anyway.
  if (colonPos == std::string::npos) {
    return username;
  }

  return username.substr(0, colonPos);
}
std::shared_ptr<ConnectionFactory::Connection>
ConnectionFactory::FindFromStunRequest(const uint8_t *p, size_t sz) {
  auto packet = std::unique_ptr<RTC::StunPacket>(RTC::StunPacket::Parse(p, sz));
  if (packet == nullptr) {
    QRPC_LOG(warn, "ignoring wrong STUN packet received");
    return nullptr;
  }
  QRPC_LOGJ(info, {{"ev","STUN packet received"},{"username",packet->GetUsername()}})
  // try to match the local ICE username fragment.
  auto key = GetLocalIceUFragFrom(*packet);
  // stun binding response from server does not contains username fragment
  // usually client session created with Connection object, no FindFromStunRequest call needed.
  // but when client give up one endpoint in SDP and using next one, packet from old endpoint might be received.
  // in that case, session for that endoint will be created again without provided connection object,
  // control flow will reach to here. so for client, we ignore that error
  ASSERT(!key.empty() || is_client());
  auto it = connections_.find(key);
  if (it == this->connections_.end()) {
    logger::warn({
      {"ev","ignoring received STUN packet with unknown remote ICE usernameFragment"},
      {"ufrag",key}
    });
    ASSERT(false);
    return nullptr;
    // validate packet is properly authorized    
  } else if (!it->second->ice_server().ValidatePacket(*packet)) {
    logger::warn({
      {"ev","ignoring received STUN packet that does not have proper token or not binding request"},
      {"ufrag",key}
    });
    return nullptr;
  }
  return it->second;
}
void ConnectionFactory::RegisterCname(
  const std::string &cname, std::shared_ptr<Connection> &c) {
  auto prevcit = cnmap_.find(cname);
  bool prev_exists = false;
  std::string prev_uflag = "";
  if (prevcit != cnmap_.end() && prevcit->second->ufrag() != c->ufrag()) {
    QRPC_LOGJ(info, {
      {"ev","previous connection exists"},{"prev",prevcit->second->ufrag()},
      {"now",c->ufrag()},{"ptr",str::dptr(prevcit->second.get())}
    });
    // cleanup old one
    prevcit->second->Close();
    prev_exists = true;
    prev_uflag = prevcit->second->ufrag();
  }
  QRPC_LOGJ(info, {{"ev","register connection"},{"ufrag",c->ufrag()},{"cname",cname},{"ptr",str::dptr(c.get())}});
  cnmap_[cname] = c;
  auto newcit = cnmap_.find(cname);
  QRPC_LOGJ(info, {{ "ev", "new connection registered" }, { "ptr", str::dptr(newcit->second.get()) },{"now",newcit->second->ufrag()}});
  if (prev_exists) {
    if (prev_uflag == newcit->second->ufrag()) {
      ASSERT(false);
    }
  }
}
std::shared_ptr<rtp::Handler>
ConnectionFactory::FindHandler(const std::string &cname) {
  auto it = cnmap_.find(cname);
  if (it == cnmap_.end()) {
    // TODO: now it targets peer that connected to the node. to scale, we have to be able to watch remote peer
    // this should be achieved like following:
    // 1. create kind of 'proxy connection' that acts like rtp::Handler::Listener
    //   - this proxy connection connects the node that has peer which id is `cname`
    //   - so maybe this function acts like async function because it needs to query remote controller which knows where `cname` is
    // 2. create handler with that proxy connection
    QRPC_LOGJ(info, {{"ev","peer not found"},{"cname",cname}});
    return nullptr;
  }
  return it->second->rtp_handler_;
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
static Channel::ChannelSocket g_channel_socket_(INVALID_FD, INVALID_FD);
static void srtp_logger(srtp_log_level_t level,
                        const char *msg,
                        void *data) {
  QRPC_LOGJ(info,{{"ev","srtp log"},{"level",level},{"msg",msg}});
}
int ConnectionFactory::GlobalInit(AlarmProcessor &a) {
	try
	{
    std::lock_guard<std::mutex> lock(g_ref_sync_mutex_);
    if (g_ref_count_ == 0) {
      // setup RTC::Timer and UnixStreamSocket, Logger, rtp::Parameters
      rtp::Parameters::SetupHeaderExtensionMap();
      ::TimerHandle::SetTimerProc(
        [&a](const ::TimerHandle::Handler &h, uint64_t start_at) {
          return a.Set([hh = h]() {
            auto intv = hh();
            if (intv <= 0) {
              return 0ULL;
            }
            return qrpc_time_now() + qrpc_time_msec(intv);
          }, qrpc_time_now() + qrpc_time_msec(start_at));
        },
        [&a](uint64_t id) {
          return a.Cancel(id);
        }
      );
      UnixStreamSocketHandle::SetWriter(
        [](const uint8_t *p, size_t sz) {
          // p should generated with ::flatbuffers::FinishSizePrefixed
          auto *msg = ::flatbuffers::GetSizePrefixedRoot<FBS::Message::Message>(p);
          switch (msg->data_type()) {
            case FBS::Message::Body::Request: {
              auto *req = msg->data_as_Request();
              QRPC_LOGJ(info,{{"ev","rtp req"},{"type",req->body_type()}});
              ASSERT(false);
            } break;
            case FBS::Message::Body::Response: {
              auto *res = msg->data_as_Response();
              QRPC_LOGJ(info,{{"ev","rtp res"},{"type",res->body_type()}});
              switch (res->body_type()) {
                case FBS::Response::Body::NONE:
                case FBS::Response::Body::Transport_ProduceResponse:
                case FBS::Response::Body::Transport_ConsumeResponse:
                  return;
                default:
                  ASSERT(false);
              }
            } break;
            case FBS::Message::Body::Notification: {
              auto *n = msg->data_as_Notification();
              QRPC_LOGJ(info,{{"ev","rtp notify"},{"type",n->body_type()}});
            } break;
            case FBS::Message::Body::Log: {
              auto *log = msg->data_as_Log();
              QRPC_LOGJ(info,{{"ev","rtp log"},{"msg",log->data()->str()}});
            } break;
            default:
              ASSERT(false);
              break;
          }
        }
      );      
      std::string llv = "debug";
      rtp::Handler::ConfigureLogging(llv, {"rtp", "rtcp"});
      // Initialize static stuff.
      DepOpenSSL::ClassInit();
      DepLibSRTP::ClassInit();
      DepUsrSCTP::ClassInit();
      DepLibWebRTC::ClassInit();
      Utils::Crypto::ClassInit();
      RTC::DtlsTransport::ClassInit();
      srtp_install_log_handler(srtp_logger, nullptr);
      RTC::SrtpSession::ClassInit();
      Logger::ClassInit(&g_channel_socket_);
      DepUsrSCTP::CreateChecker();
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
		RTC::DtlsTransport::ClassDestroy();
		Utils::Crypto::ClassDestroy();
		DepLibWebRTC::ClassDestroy();
		DepUsrSCTP::ClassDestroy();
		DepLibSRTP::ClassDestroy();
  }
  catch (const MediaSoupError& error) {
    logger::error({{"ev","mediasoup cleanup failure"},{"reason",error.what()}});
	}
}
std::shared_ptr<Connection> ConnectionFactory::Create(
  RTC::DtlsTransport::Role dtls_role, std::string &ufrag, std::string &pwd,
  bool do_entry
) {
  auto c = std::shared_ptr<Connection>(factory_method_(*this, dtls_role));
  if (c == nullptr) {
    logger::error({{"ev","fail to allocate connection"}});
    return nullptr;
  }
  int r;
  if ((r = c->Init(ufrag, pwd)) < 0) {
    logger::error({{"ev","fail to init connection"},{"rc",r}});
    return nullptr;
  }
  if (do_entry) {
    connections_[ufrag] = c;
  }
  return c;
}

// ConnectionFactory::Config
int ConnectionFactory::Config::Derive() {
  for (auto fp : RTC::DtlsTransport::GetLocalFingerprints()) {
    auto fpit = RTC::DtlsTransport::GetString2FingerprintAlgorithm().find(fingerprint_algorithm);
    if (fpit == RTC::DtlsTransport::GetString2FingerprintAlgorithm().end()) {
      logger::die({{"ev","invalid fingerprint algorithm name"},{"algo", fingerprint_algorithm}});
      return QRPC_EDEPS;
    }
    // TODO: SHA256 is enough?
    if (fp.algorithm == fpit->second) {
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
    if (ifaddrs.size() <= 0) {
      logger::die({{"ev","no if address detected"},{"in6",in6}});
      return QRPC_EDEPS;
    }
  } else {
    logger::info({{"ev","add configured ip"},{"ip",ip}});
    ifaddrs.push_back(ip);
  }
  return QRPC_OK;
}

// ConnectionFactory::UdpSession/TcpSession
template <class PS>
int ConnectionFactory::TcpSessionTmpl<PS>::OnRead(const char *p, size_t sz) {
  auto up = reinterpret_cast<const uint8_t *>(p);
  if (connection_ == nullptr) {
    connection_ = connection_factory().FindFromStunRequest(up, sz);
    if (connection_ == nullptr) {
      QRPC_LOGJ(info, {{"ev","fail to find connection from stun request"}});
      return QRPC_EINVAL;
    }
  } else if (connection_->closed()) {
    QRPC_LOGJ(info, {{"ev","parent connection closed, remove the session"},{"from",PS::addr().str()}});
    return QRPC_EGOAWAY;
  }
  return connection_->OnPacketReceived(this, up, sz);
}
template <class PS>
qrpc_time_t ConnectionFactory::TcpSessionTmpl<PS>::OnShutdown() {
  if (connection_ != nullptr) {
    connection_->OnTcpSessionShutdown(this);
  }
  return 0;
}
template <class PS>
int ConnectionFactory::UdpSessionTmpl<PS>::OnRead(const char *p, size_t sz) {
  auto up = reinterpret_cast<const uint8_t *>(p);
  if (connection_ == nullptr) {
    connection_ = connection_factory().FindFromStunRequest(up, sz);
    if (connection_ == nullptr) {
      QRPC_LOGJ(info, {{"ev","fail to find connection from stun request"}});
      return QRPC_EINVAL;
    }
  }
  // UdpSession is not closed because fd is shared among multiple sessions
  return connection_->OnPacketReceived(this, up, sz);
}
template <class PS>
qrpc_time_t ConnectionFactory::UdpSessionTmpl<PS>::OnShutdown() {
  if (connection_ != nullptr) {
    connection_->OnUdpSessionShutdown(this);
  }
  return 0;
}

/* ConnectionFactory::SyscallStream */
#define RAISE(str) { \
  std::string __error = std::string(str) + " at " +  __FILE__ +  ":" + LINESTR; \
  QRPC_LOGJ(error, {{"ev","syscall failure"},{"fn",fn},{"pl",pl},{"error",__error}}); \
  Call((fn + "_ack").c_str(), msgid, {{"error",__error}}); \
  return QRPC_OK; \
}
static std::map<std::string, logger::level> syscall_log_levels = {
  {"ping", logger::level::trace},
};
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
    const auto mit = data.find("msgid");
    if (mit == data.end()) {
      QRPC_LOGJ(error, {{"ev","syscall invalid payload"},{"fn",fn},{"pl",pl},{"r","no value for key 'msgid'"}});
      return QRPC_OK;
    }
    const auto msgid = mit->get<uint64_t>();
    auto &c = dynamic_cast<Connection &>(connection());
    auto scllvit = syscall_log_levels.find(fn);
    if (scllvit != syscall_log_levels.end()) {
      QRPC_LOGVJ(scllvit->second, {{"ev","recv syscall"},{"pl",data}});
    } else {
      QRPC_LOGJ(info, {{"ev","recv syscall"},{"pl",data}});
    }
    try {
      if (fn == "close") {
        QRPC_LOGJ(info, {{"ev", "shutdown from peer"}});
        c.factory().ScheduleClose(c);
        Call("close_ack",msgid,{});
      } else {
        const auto ait = data.find("args");
        if (ait == data.end()) {
          RAISE("no value for key 'args'");
        }
        const auto &args = ait->get<std::map<std::string,json>>();
        if (fn == "produce") {
          const auto sdpit = args.find("sdp");
          if (sdpit == args.end()) {
            RAISE("no value for key 'sdp'");
          }
          const auto mpmit = args.find("midPathMap");
          if (mpmit == args.end()) {
            RAISE("no value for key 'midPathMap'");
          }
          std::map<rtp::Parameters::MediaKind, ControlOptions> options_map;
          const auto oit = args.find("options");
          if (oit != args.end()) {
            QRPC_LOGJ(info, {{"ev","produce options"},{"options",oit->second}});
            const auto &opts = oit->second.get<std::map<std::string,json>>();
            const auto v = opts.find("video");
            if (v != opts.end()) {
              options_map.emplace(rtp::Parameters::MediaKind::VIDEO, v->second);
            }
            const auto a = opts.find("audio");
            if (a != opts.end()) {
              options_map.emplace(rtp::Parameters::MediaKind::AUDIO, a->second);
            }
          }
          const auto rtpit = args.find("rtp");
          if (rtpit != args.end()) {
            c.InitRTP();
            c.rtp_handler().SetNegotiationArgs(rtpit->second.get<std::map<std::string,json>>());
          }
          SDP sdp(sdpit->second.get<std::string>());
          std::string answer;
          std::map<std::string,rtp::Producer*> created_producers;
          if (!sdp.Answer(mpmit->second.get<std::map<std::string,std::string>>(), c, answer, &options_map, &created_producers)) {
            RAISE("fail to prepare consume");
          }
          if (!c.rtp_enabled()) {
            RAISE("nothing produced");
          }
          json status_map;
          for (const auto &kv : created_producers) {
            status_map[kv.first] = kv.second->status().ToJson();
          }
          Call("produce_ack",msgid,{{"sdp",answer},{"status_map",status_map},{"mid_media_path_map",c.rtp_handler().mid_media_path_map()}});
        } else if (fn == "consume") {
          QRPC_LOGJ(info, {{"ev","consume request"},{"args",args}});
          const auto pit = args.find("path");
          if (pit == args.end()) {
            RAISE("no value for key 'path'");
          }
          std::map<rtp::Parameters::MediaKind, ControlOptions> options_map;
          bool sync = false;
          const auto oit = args.find("options");
          if (oit != args.end()) {
            QRPC_LOGJ(info, {{"ev","consume options"},{"options",oit->second}});
            const auto &opts = oit->second.get<std::map<std::string,json>>();
            const auto v = opts.find("video");
            if (v != opts.end()) {
              options_map.emplace(rtp::Parameters::MediaKind::VIDEO, v->second);
            }
            const auto a = opts.find("audio");
            if (a != opts.end()) {
              options_map.emplace(rtp::Parameters::MediaKind::AUDIO, a->second);
            }
            const auto syncit = opts.find("sync");
            if (syncit != opts.end()) {
              sync = syncit->second.get<bool>();
            }
          }
          auto path = pit->second.get<std::string>();
          std::string sdp;
          std::map<uint32_t,std::string> ssrc_label_map;
          std::map<std::string,rtp::Consumer*> created_consumers;
          if (!c.PrepareConsume(path, options_map, sync, sdp, ssrc_label_map, created_consumers)) {
            RAISE("fail to prepare consume");
          }
          json status_map;
          for (const auto &kv : created_consumers) {
            auto st = rtp::ConsumerFactory::StatusFrom(kv.second);
            status_map[kv.first] = st.ToJson();
          }
          Call("consume_ack",msgid,{
            {"ssrc_label_map",ssrc_label_map},{"status_map",status_map},
            {"mid_media_path_map",c.rtp_handler().mid_media_path_map()},{"sdp",sdp}
          });
        } else if (fn == "pause") {
          const auto pit = args.find("path");
          if (pit == args.end()) {
            RAISE("no value for key 'path'");
          }
          std::string reason;
          if (!c.rtp_handler().Pause(pit->second.get<std::string>(), reason)) {
            RAISE("fail to pause track:" + reason);
          }
          Call("pause_ack",msgid,{});
        } else if (fn == "resume") {
          const auto pit = args.find("path");
          if (pit == args.end()) {
            RAISE("no value for key 'path'");
          }
          std::string reason;
          if (!c.rtp_handler().Resume(pit->second.get<std::string>(), reason)) {
            RAISE("fail to pause track:" + reason);
          }
          Call("resume_ack",msgid,{});
        } else if (fn == "sync") {
          const auto pit = args.find("path");
          if (pit == args.end()) {
            RAISE("no value for key 'path'");
          }
          std::string sdp;
          if (!c.rtp_handler().Sync(pit->second.get<std::string>(), sdp)) {
            RAISE("fail to sync:" + sdp);
          }
          Call("sync_ack",msgid,{
            {"mid_media_path_map",c.rtp_handler().mid_media_path_map()},{"sdp",sdp}
          });
        } else if (fn == "ping") {
          std::string error;
          if (!c.rtp_handler().Ping(error)) {
            RAISE("fail to ping:" + error);
          }
          Call("ping_ack",msgid,{},logger::level::trace);
        } else {
          RAISE("syscall is not supported");
        }
      }
    } catch (const std::exception& error) {
      RAISE(error.what());
    }
  } catch (const std::exception& error) {
    QRPC_LOGJ(error, {{"ev","syscall invalid payload"},{"err",error.what()}});
  }
  return QRPC_OK;
}
int ConnectionFactory::SyscallStream::Call(const char *fn) {
  return Send({{"fn",fn}});
}
int ConnectionFactory::SyscallStream::Call(const char *fn, const json &j) {
  return Send({{"fn",fn},{"args",j}});
}
int ConnectionFactory::SyscallStream::Call(const char *fn, uint32_t msgid, const json &j, logger::level llv) {
  QRPC_LOGVJ(llv, {{"ev","syscall response"},{"fn",fn},{"msgid",msgid},{"args",j}})
  return Send({{"fn",fn},{"msgid",msgid},{"args",j}});
}

/* ConnectionFactory::Connection */
bool ConnectionFactory::Connection::connected() const {
  return (
    (
      ice_server_->GetState() == IceServer::IceState::CONNECTED ||
      ice_server_->GetState() == IceServer::IceState::COMPLETED
    ) && dtls_transport_->GetState() == RTC::DtlsTransport::DtlsState::CONNECTED
  );
}
void ConnectionFactory::Connection::InitRTP() {
  if (rtp_handler_ == nullptr) {
    rtp_handler_ = std::make_shared<rtp::Handler>(*this);
    for (const auto &kv : capabilities_) {
      rtp_handler_->UpdateByCapability(kv.second);
    }
  }
}
bool ConnectionFactory::Connection::PrepareConsume(
  const std::string &media_path, 
  const std::map<rtp::Parameters::MediaKind, ControlOptions> &options_map, bool sync,
  std::string &sdp, std::map<uint32_t,std::string> &ssrc_label_map,
  std::map<std::string,rtp::Consumer*> &created_consumers
) {
  // TODO: support fullpath like $url/@cname/name. first should remove part before /@
  auto parsed = str::Split(media_path, "/");
  if (parsed.size() < 3) {
    // TODO: support self consume. this may be useful for syhncronizing audio/video in server side
    // but using $my_cnam/$path for self consume might be enough
    QRPC_LOGJ(error, {{"ev","invalid media_path"},{"media_path",media_path}});
    ASSERT(false);
    return false;
  }
  auto h = factory().FindHandler(parsed[0]);
  if (h == nullptr) {
    sdp = "peer not found: " + parsed[0];
    QRPC_LOGJ(error, {{"ev","peer not found"},{"cname",parsed[0]}});
    return false;
  }
  parsed.erase(parsed.begin());
  const auto &last_component = parsed[parsed.size() - 1];
  const auto media_kind = rtp::Parameters::ToMediaKind(last_component);
  if (!media_kind.has_value()) {
    if (!last_component.empty()) {
      sdp = "invalid media_kind: " + last_component;
      QRPC_LOGJ(error, {{"ev","invalid media_kind"},{"kind",last_component}});
      ASSERT(false);
      return false;
    }
  } else {
    // empty last element to generate directory path
    parsed[parsed.size() - 1] = "";
  }
  auto &mscs = media_stream_configs();
  std::vector<uint32_t> generated_ssrcs;
  InitRTP();
  if (rtp_handler().PrepareConsume(*h, str::Join(parsed, "/"), media_kind, 
    options_map, sync, mscs, generated_ssrcs, created_consumers)) {
    for (const auto ssrc : generated_ssrcs) {
      ssrc_label_map[ssrc] = media_path;
    }
    auto proto = ice_server().GetSelectedSession()->proto();
    if (!SDP::GenerateAnswer(*this, proto, mscs, sdp)) {
      QRPC_LOGJ(error, {{"ev","fail to generate sdp"},{"reason",sdp}});
      ASSERT(false);
      return false;
    }
    if (IsConnected()) {
      QRPC_LOGJ(debug, {{"ev","consumer connection already ready. consume now"}});
      return Consume(created_consumers, sdp);
    }
    return true;
  } else {
    ASSERT(false);
    return false;
  }
}
bool ConnectionFactory::Connection::Consume(std::map<std::string,rtp::Consumer*> &created_consumers, std::string &error) {
  if (!is_consumer()) {
    error = "there should be consumer config";
    QRPC_LOGJ(error, {{"ev",error}});
    ASSERT(false);
    return false;
  }
  // force initiate rtp
  InitRTP();
  for (const auto &c : media_stream_configs()) {
    if (!ConsumeMedia(c, error)) {
      return false;
    } else if (created_consumers.find(c.media_path) != created_consumers.end()) {
      created_consumers[c.media_path] = rtp_handler().FindConsumerByPath(c.media_path);
      if (created_consumers[c.media_path] == nullptr) {
        error = "fail to find consumer by path: " + c.media_path;
        ASSERT(false);
        return false;
      }
    }
  }
  return true;
}
bool ConnectionFactory::Connection::ConsumeMedia(
  const rtp::MediaStreamConfig &config, std::string &error
) {
  if (config.mid == RTC::RtpProbationGenerator::GetMidValue()) {
    QRPC_LOGJ(debug, {{"ev","ignore probator"}});
    return true;
  }
  if (!config.sender()) {
    QRPC_LOGJ(debug, {{"ev","ignore non-consume config"},{"mid",config.mid},{"path",config.media_path}});
    return true;
  }
  ASSERT(is_consumer());
  // TODO: support fullpath like $url/@cname/name. first should remove part before /@
  auto parsed = str::Split(config.media_path, "/");
  if (parsed.size() < 2) {
    error = "invalid media_path: " + config.media_path;
    QRPC_LOGJ(error, {{"ev","invalid media_path"},{"path",config.media_path}});
    ASSERT(false);
    return false;
  }
  auto h = factory().FindHandler(parsed[0]);
  if (h == nullptr) {
    // this may not error (target peer gone). cannot remove because client reconnection still on going
    QRPC_LOGJ(warn, {{"ev","ignore because peer not found"},{"cname",parsed[0]}});
    return true;
  }
  std::vector<uint32_t> generated_ssrcs;
  if (rtp_handler().Consume(*h, config, error)) {
    return true;
  } else {
    return false;
  }
}
int ConnectionFactory::Connection::Init(std::string &ufrag, std::string &pwd) {
  if (ice_server_ != nullptr) {
    logger::warn({{"ev","already init"}});
    return QRPC_OK;
  }
  ufrag = random::word(32);
  pwd = random::word(32);
  // create ICE server
  ice_server_.reset(new IceServer(this, ufrag, pwd, factory().config().consent_check_interval));
  if (ice_server_ == nullptr) {
    logger::die({{"ev","fail to create ICE server"}});
    return QRPC_EALLOC;
  }
  // create DTLS transport
  try {
    dtls_transport_.reset(new RTC::DtlsTransport(this));
  } catch (const MediaSoupError &error) {
    logger::error({{"ev","fail to create DTLS transport"},{"reason",error.what()}});
    return QRPC_EALLOC;
  }
  // create SCTP association
  sctp_association_.reset(
    new RTC::SctpAssociation(
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
void ConnectionFactory::Connection::SetCname(const std::string &cname) {
  ASSERT(cname_.empty());
  cname_ = cname;
}
bool ConnectionFactory::Connection::SetRtpCapability(const std::string &cap_sdp, std::string &answer) {
  SDP sdp(cap_sdp);
  for (const auto k : rtp::Handler::SupportedMediaKind()) {
    json section;
    if (!sdp.FindMediaSection(rtp::Parameters::FromMediaKind(k), section)) {
      answer = "capability should contain all media infromation";
      QRPC_LOGJ(error, {{"ev","cap_sdp parse error"},{"error",answer},{"sdp",cap_sdp}});
      return false;
    }
    auto pair = capabilities_.emplace(k, rtp::Capability());
    if (!pair.second) {
      answer = "capability already set";
      QRPC_LOGJ(error, {{"ev",answer},{"media",rtp::Parameters::FromMediaKind(k)}});
      return false;
    }
    auto &cap = pair.first->second;
    rtp::Parameters params;
    if (!params.Parse(section, cap, answer)) {
      answer = "ail to parse section";
      QRPC_LOGJ(error, {{"ev","section parse error"},{"error",answer},{"section",section}});
      return false;
    }
  }
  return true;
}

void ConnectionFactory::Connection::RegisterCname() {
  // we need to use existing std::shared_ptr. because if we insert `this` to ConnectionFactory::cnmap_ directly,
  // 2 different family of std::shared_ptr (another one is ConnectionFactory::connections_) try to free `this` independently.
  auto c = factory().FindFromUfrag(ufrag());
  if (c == nullptr) {
    ASSERT(false);
    return;
  }
  // TODO: valiate cname before register to cname map
  factory().RegisterCname(cname_, c);
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
  streams_[s->id()] = s;
  return s;
}
StreamFactory ConnectionFactory::Connection::DefaultStreamFactory() {
  return [this](const Stream::Config &config, base::Connection &conn) -> std::shared_ptr<Stream> {
    if (config.label == Stream::SYSCALL_NAME) {
      return this->syscall_ = std::make_shared<SyscallStream>(conn, config);
    } else {
      return this->factory().stream_factory()(config, conn);
    }
  };
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
  sctp_association_->HandleDataConsumer(s->id());
  // send DCEP OPEN messsage to peer
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
  if (rtp_handler_ != nullptr) {
    rtp_handler_->Disconnected();
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
  if (rtp_enabled()) {
    QRPC_LOGJ(info, {{"ev","close rtp handler"},{"id",rtp_handler().rtp_id()}});
    rtp_handler().Close();
  }
  if (syscall_ == nullptr) {
    syscall_ = std::dynamic_pointer_cast<SyscallStream>(OpenStream({
      .label = Stream::SYSCALL_NAME
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

    default: {
      logger::error({{"ev","invalid local DTLS role"},{"role",dtls_role_}});
      return QRPC_EINVAL;
    }
  }
  return QRPC_OK;
}
void ConnectionFactory::Connection::OnDtlsEstablished() {
  sctp_association_->TransportConnected();
  if (rtp_handler_ != nullptr) {
    rtp_handler_->Connected();
  }
  int r;
  if ((r = OnConnect()) < 0) {
    logger::error({{"ev","application reject connection"},{"rc",r}});
    factory().ScheduleClose(*this);
  }
}
void ConnectionFactory::Connection::OnTcpSessionShutdown(Session *s) {
  ice_server_->RemoveSession(s);
}
void ConnectionFactory::Connection::OnUdpSessionShutdown(Session *s) {
  ice_server_->RemoveSession(s);
}

int ConnectionFactory::Connection::OnPacketReceived(Session *session, const uint8_t *p, size_t sz) {
  // Check if it's STUN.
  Touch(qrpc_time_now());
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
  if (!ice_server_->IsValidSession(session)) {
    logger::warn({{"ev","ignoring DTLS data coming from an invalid session"},{"proto","dtls"}});
    return QRPC_OK;
  }
  // Trick for clients performing aggressive ICE regardless we are ICE-Lite.
  ice_server_->MayForceSelectedSession(session);
  // Check that DTLS status is 'connecting' or 'connected'.
  if (
    dtls_transport_->GetState() == RTC::DtlsTransport::DtlsState::CONNECTING ||
    dtls_transport_->GetState() == RTC::DtlsTransport::DtlsState::CONNECTED) {
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
  auto decrypted = srtp_recv_->DecryptSrtcp(const_cast<uint8_t *>(p), &sz);
  if (!decrypted) {
    QRPC_LOGJ(warn, {{"proto","srtcp"},
      {"ev","received data is not a valid RTP packet"},
      {"decrypted",decrypted},{"len",sz},
      {"pl",str::HexDump(p, std::min((size_t)32, sz))}});
    ASSERT(false);
    return;
  }
  RTC::RTCP::Packet* packet = RTC::RTCP::Packet::Parse(p, sz);
  if (packet == nullptr) {
    logger::warn({{"proto","srtcp"},
      {"ev","received data is not a valid RTCP compound or single packet"},
      {"pl",str::HexDump(p, std::min((size_t)32, sz))}});
    return;
  }
  // we need to implement RTC::Transport::ReceiveRtcpPacket(packet) equivalent
  // logger::info({{"ev","RTCP packet received"}});
  rtp_handler_->ReceiveRtcpPacket(packet); // deletes packet
}
int ConnectionFactory::Connection::OnRtcpDataReceived(Session *session, const uint8_t *p, size_t sz) {
  TRACK();
  // Ensure DTLS is connected.
  if (dtls_transport_->GetState() != RTC::DtlsTransport::DtlsState::CONNECTED) {
    logger::debug({{"ev","ignoring RTCP packet while DTLS not connected"},{"proto","dtls,rtcp"}});
    return QRPC_OK;
  }
  // Ensure there is receiving SRTP session.
  if (srtp_recv_ == nullptr) {
    logger::debug({{"proto","srtp"},{"ev","ignoring RTCP packet due to non receiving SRTP session"}});
    return QRPC_OK;
  }
  // Ensure it comes from a valid tuple.
  if (!ice_server_->IsValidSession(session)) {
    logger::warn({{"proto","rtcp"},{"ev","ignoring RTCP packet coming from an invalid tuple"}});
    return QRPC_OK;
  }
  // Decrypt the SRTCP packet.
  TryParseRtcpPacket(p, sz);
  return QRPC_OK;
}
void ConnectionFactory::Connection::TryParseRtpPacket(const uint8_t *p, size_t sz) {
  // Decrypt the SRTP packet.
  auto decrypted = this->srtp_recv_->DecryptSrtp(const_cast<uint8_t*>(p), &sz);
  auto *packet = RTC::RtpPacket::Parse(p, sz);
  if (packet == nullptr) {
    QRPC_LOGJ(warn, {{"proto","rtcp"},
      {"ev","received data is not a valid RTP packet"},
      {"decrypted",decrypted},{"len",sz},
      {"pl",str::HexDump(p, std::min((size_t)32, sz))}});
    ASSERT(false);
    return;
  }
  if (!decrypted) {
    QRPC_LOGJ(warn, {
      {"ev","RTP packet received, but decryption fails"},
      {"proto","srtp"},{"ssrc",packet->GetSsrc()},
      {"payloadType",packet->GetPayloadType()},{"seq",packet->GetSequenceNumber()}
    });
    delete packet;
  } else {
    rtp_handler_->ReceiveRtpPacket(packet); // deletes packet
  }
}
int ConnectionFactory::Connection::OnRtpDataReceived(Session *session, const uint8_t *p, size_t sz) {
  TRACK();
  // Ensure DTLS is connected.
  if (dtls_transport_->GetState() != RTC::DtlsTransport::DtlsState::CONNECTED) {
    logger::debug({{"ev","ignoring RTCP packet while DTLS not connected"},{"proto","dtls,rtcp"}});
    return QRPC_OK;
  }
  // Ensure there is receiving SRTP session.
  if (srtp_recv_ == nullptr) {
    logger::debug({{"proto","srtp"},{"ev","ignoring RTCP packet due to non receiving SRTP session"}});
    return QRPC_OK;
  }
  // Ensure it comes from a valid tuple.
  if (!ice_server_->IsValidSession(session)) {
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
  sctp_association_->SendSctpMessage(s.config().params, reinterpret_cast<const uint8_t *>(p), sz, ppid);
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
    // bool isOutgoing = (dtls_role_ == RTC::DtlsTransport::Role::CLIENT) == ((s.id() % 2) == 0);
    sctp_association_->DataConsumerClosed(s.id());
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
      s.config().params, req.ToPaylod(buff, sizeof(buff)), req.PayloadSize(), PPID::WEBRTC_DCEP
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
  factory_.ScheduleClose(usernameFragment);
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
  if (dtls_transport_->GetState() == RTC::DtlsTransport::DtlsState::CONNECTED) {
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
  if (!ice_prober_ || dtls_role_ == RTC::DtlsTransport::Role::CLIENT) {
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
void ConnectionFactory::Connection::OnDtlsTransportConnecting(const RTC::DtlsTransport* dtlsTransport) {
  TRACK();
}
void ConnectionFactory::Connection::OnDtlsTransportConnected(
  const RTC::DtlsTransport* dtlsTransport,
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
void ConnectionFactory::Connection::OnDtlsTransportFailed(const RTC::DtlsTransport* dtlsTransport) {
  logger::info({{"ev","tls failed"}});
  OnDtlsTransportClosed(dtlsTransport);
}
// The DTLS connection has been closed due to receipt of a close_notify alert.
void ConnectionFactory::Connection::OnDtlsTransportClosed(const RTC::DtlsTransport* dtlsTransport) {
  logger::info({{"ev","tls closed"}});
  // Tell the parent class. (if we handle srtp, need to implement equivalent)
  // RTC::Transport::Disconnected();
  // above notifies TransportCongestionControlClient and TransportCongestionControlServer
  // may need to implement equivalent for performance
  factory().ScheduleClose(*this); // this might be freed here, so don't touch after the line
}
// Need to send DTLS data to the peer.
void ConnectionFactory::Connection::OnDtlsTransportSendData(
  const RTC::DtlsTransport* dtlsTransport, const uint8_t* data, size_t len) {
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
  const RTC::DtlsTransport*, const uint8_t* data, size_t len) {
  TRACK();
  sctp_association_->ProcessSctpData(data, len);
}

// implements RTC::SctpAssociation::Listener
void ConnectionFactory::Connection::OnSctpAssociationConnecting(RTC::SctpAssociation* sctpAssociation) {
  TRACK();
  // only notify
}
void ConnectionFactory::Connection::OnSctpAssociationConnected(RTC::SctpAssociation* sctpAssociation) {
  TRACK();
  sctp_connected_ = true;
}
void ConnectionFactory::Connection::OnSctpAssociationFailed(RTC::SctpAssociation* sctpAssociation) {
  TRACK();
  sctp_connected_ = false;
  // TODO: notify app
}
void ConnectionFactory::Connection::OnSctpAssociationClosed(RTC::SctpAssociation* sctpAssociation) {
  TRACK();
  sctp_connected_ = false;
  // TODO: notify app
}
void ConnectionFactory::Connection::OnSctpStreamReset(
  RTC::SctpAssociation* sctpAssociation, uint16_t streamId) {
  auto s = streams_.find(streamId);
  if (s == streams_.end()) {
    logger::error({{"proto","sctp"},{"ev","reset stream not found"},{"sid",streamId}});
    return;
  }
  s->second->OnShutdown();
  streams_.erase(s);
}
void ConnectionFactory::Connection::OnSctpAssociationSendData(
  RTC::SctpAssociation* sctpAssociation, const uint8_t* data, size_t len) {
  TRACK();
  if (!connected()) {
		logger::warn({{"proto","sctp"},{"ev","DTLS not connected, cannot send SCTP data"},
      {"dtls_state",dtls_transport_->GetState()}});
    return;
  }
  // logger::debug({{"proto","sctp"},{"ev","send data"},{"sz",len}});
  dtls_transport_->SendApplicationData(data, len);
}
void ConnectionFactory::Connection::OnSctpWebRtcDataChannelControlDataReceived(
  RTC::SctpAssociation* sctpAssociation,
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
    auto c = req->ToMediaStreamConfig();
    auto s = NewStream(c, DefaultStreamFactory());
    if (s == nullptr) {
      logger::error({{"proto","sctp"},{"ev","fail to create stream"},{"stream_id",streamId}});
      return;
    }
    // send dcep ack
    DcepResponse ack;
    uint8_t buff[ack.PayloadSize()];
    if ((r = sctpAssociation->SendSctpMessage(
        s->config().params, ack.ToPaylod(buff, sizeof(buff)), ack.PayloadSize(), PPID::WEBRTC_DCEP
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
  RTC::SctpAssociation* sctpAssociation,
  uint16_t streamId,
  const uint8_t* msg,
  size_t len, uint32_t ppid) {
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
  RTC::SctpAssociation* sctpAssociation, uint32_t len) {
  TRACK();
}

// implements rtp::Handler::Listener
const std::string &ConnectionFactory::Connection::FindRtpIdFrom(std::string &cname) {
  static std::string empty;
  auto h = factory().FindHandler(cname);
  if (h == nullptr) {
    return empty;
  }
  return h->rtp_id();
}
int ConnectionFactory::Connection::SendToStream(
  const std::string &label, const char *data, size_t len
) {
  bool found = false;
  for (auto &s : streams_) {
    // QRPC_LOGJ(info, {{"ev","send to stream"},{"label",s.second->config().label}});
    // currently, if multiple streams with same label, send to all of them
    if (s.second->config().label == label) {
      int r = s.second->Send(data, len);
      ASSERT(r >= 0);
      found = true;
    }
  }
  // if not found, create new stream
  if (!found && (OpenStream({
    .label = label
  }, [this, data, len, &label](const Stream::Config &config, base::Connection &conn) {
    auto s = this->DefaultStreamFactory()(config, conn);
    QRPC_LOGJ(info, {{"ev","open and send to stream"},{"label",s->config().label},{"flabel",label}});
    s->Send(data, len);
    return s;
  }) == nullptr)) {
    return QRPC_EALLOC;
  }
  return QRPC_OK;
}      

void ConnectionFactory::Connection::RecvStreamClosed(uint32_t ssrc) {
  if (srtp_recv_ != nullptr) {
    QRPC_LOGJ(info, {{"ev","recv stream closed"},{"ssrc",ssrc},{"cname",cname_}});
    srtp_recv_->RemoveStream(ssrc);
  }
}
void ConnectionFactory::Connection::SendStreamClosed(uint32_t ssrc) {
  if (srtp_send_ != nullptr) {
    QRPC_LOGJ(info, {{"ev","send stream closed"},{"ssrc",ssrc},{"cname",cname_}});
    srtp_send_->RemoveStream(ssrc);
  }
}
bool ConnectionFactory::Connection::IsConnected() const {
  return this->ice_server_->GetSelectedSession() != nullptr;
}
void ConnectionFactory::Connection::SendRtpPacket(
  RTC::Consumer* consumer, RTC::RtpPacket* packet, onSendCallback* cb) {
  MS_TRACE();

  if (!IsConnected()) {
    if (cb) {
      (*cb)(false);
      delete cb;
    }
    return;
  }

  // Ensure there is sending SRTP session.
  if (!srtp_send_) {
    logger::warn("ignoring RTP packet due to non sending SRTP session");
    if (cb) {
      (*cb)(false);
      delete cb;
    }
    return;
  }
  const uint8_t* data = packet->GetData();
  auto sz         = packet->GetSize();
  if (!srtp_send_->EncryptRtp(&data, &sz)) {
    if (cb) {
      (*cb)(false);
      delete cb;
    }
    return;
  }
  if (ice_server_->GetSelectedSession()->Send(reinterpret_cast<const char *>(data), sz) < 0) {
    if (cb) {
      (*cb)(false);
      delete cb;
    }
    return;
  }
  // packet->Dump();
  // Increase send transmission.
  rtp_handler_->DataSent(sz);
}
void ConnectionFactory::Connection::SendRtcpPacket(RTC::RTCP::Packet* packet) {
  		MS_TRACE();

		if (!IsConnected()) {
			return;
    }
		const uint8_t* data = packet->GetData();
		auto sz         = packet->GetSize();
		// Ensure there is sending SRTP session.
		if (!this->srtp_send_) {
			QRPC_LOG(warn, "ignoring RTCP packet due to non sending SRTP session");
			return;
		}
		if (!this->srtp_send_->EncryptRtcp(&data, &sz)) {
			return;
    }
		this->ice_server_->GetSelectedSession()->Send(reinterpret_cast<const char *>(data), sz);
		// Increase send transmission.
		rtp_handler_->DataSent(sz);
}
void ConnectionFactory::Connection::SendRtcpCompoundPacket(RTC::RTCP::CompoundPacket* packet) {
    MS_TRACE();
		if (!IsConnected()) {
			return;
    }
		packet->Serialize(RTC::RTCP::Buffer);
		const uint8_t* data = packet->GetData();
		auto sz         = packet->GetSize();
		// Ensure there is sending SRTP session.
		if (!this->srtp_send_) {
			QRPC_LOG(warn, "ignoring RTCP compound packet due to non sending SRTP session");
			return;
		}
		if (!this->srtp_send_->EncryptRtcp(&data, &sz)) {
			return;
    }
		this->ice_server_->GetSelectedSession()->Send(reinterpret_cast<const char *>(data), sz);
		// Increase send transmission.
		rtp_handler_->DataSent(sz);
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
  class BaseSessionTmpl : public BASE {
  public:
    typedef typename BASE::Factory Factory;
    BaseSessionTmpl(Factory &f, Fd fd, const Address &addr, std::shared_ptr<Connection> &c,
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
      // removes Session(this session) from IceServer
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
  class TcpSession : public BaseSessionTmpl<Client::TcpSession> {
  public:
    TcpSession(Factory &f, Fd fd, const Address &addr, std::shared_ptr<Connection> &c,
      const Candidate &cand, OnIceFailure &&oif
    ) : BaseSessionTmpl<Client::TcpSession>(
      f, fd, addr, c, std::get<3>(cand), std::get<4>(cand), std::get<5>(cand), std::move(oif)
    ) {}
  };
  class UdpSession : public BaseSessionTmpl<Client::UdpSession> {
  public:
    UdpSession(Factory &f, Fd fd, const Address &addr, std::shared_ptr<Connection> &c,
      const Candidate &cand, OnIceFailure &&oif
    ) : BaseSessionTmpl<Client::UdpSession>(
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
  std::string pwd;
  auto c = Create(RTC::DtlsTransport::Role::SERVER, ufrag, pwd);
  if (c == nullptr) {
    logger::error({{"ev","fail to allocate connection"}});
    return QRPC_EALLOC;
  }
  int r;
  if ((r = SDP::Offer(*c, ufrag, pwd, sdp)) < 0) {
    logger::error({{"ev","fail to create offer"},{"rc",r}});
    return QRPC_EINVAL;
  }
  endpoints_[ufrag] = ep;
  connections_[ufrag] = c;
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
    json response_json;
    if ((r = Accept(s.fsm().body(), response_json)) < 0) {
        logger::error("fail to create connection");
        s.ServerError("server error %d", r);
        return nullptr;
    }
    std::string response = response_json.dump();
    std::string resplen = std::to_string(response.length());
    HttpHeader h[] = {
        {.key = "Content-Type", .val = "application/json"},
        {.key = "Content-Length", .val = resplen.c_str()}
    };
    s.Respond(HRC_OK, h, 2, response.c_str(), response.length());
    return nullptr;
  });
  if (!http_listener_.Listen(signaling_port, router_)) {
    logger::error({{"ev","fail to listen on signaling port"},{"port",signaling_port}});
    return false;
  }
  return true;
}
int Listener::Accept(const std::string &client_req_body, json &response) {
  try {
    auto client_req = json::parse(client_req_body);
    logger::info({{"ev","new server connection"},{"client_req", client_req}});
    auto client_sdp_it = client_req.find("sdp");
    if (client_sdp_it == client_req.end()) {
      logger::error({{"ev","fail to find sdp to answer"},{"req",client_req}});
      return QRPC_EINVAL;
    }
    auto client_sdp = client_sdp_it->get<std::string>();
    auto cnit = client_req.find("cname");
    if (cnit == client_req.end()) {
      logger::error({{"ev","fail to find value for key 'cname'"},{"req",client_req}});
      return QRPC_EINVAL;
    }
    const auto mpmit = client_req.find("midPathMap");
    if (mpmit == client_req.end()) {
      logger::error({{"ev","fail to find value for key 'midPathMap'"},{"req",client_req}});
      return QRPC_OK;
    }
    const auto capit = client_req.find("capability");
    if (capit == client_req.end()) {
      QRPC_LOGJ(error, {{"ev","fail to find value for key 'capability'"},{"req",client_req}});
      return QRPC_OK;
    }
    // server connection's dtls role is client, workaround fo osx safari (16.4) does not initiate DTLS handshake
    // even if sdp anwser ask to do it.
    std::string ufrag, pwd;
    auto c = Create(RTC::DtlsTransport::Role::CLIENT, ufrag, pwd);
    if (c == nullptr) {
      logger::error({{"ev","fail to allocate connection"}});
      return QRPC_EALLOC;
    }
    logger::info({{"ev","allocate connection"},{"ufrag",ufrag}});
    std::string answer;
    auto cap_sdp = capit->get<std::string>();
    if (!c->SetRtpCapability(cap_sdp, answer)) {
      QRPC_LOGJ(error, {{"ev","fail to parse capability"},{"capability_sdp",cap_sdp}});
      return QRPC_OK;
    }
    c->SetCname(cnit->get<std::string>());
    const auto rtpit = client_req.find("rtp");
    if (rtpit != client_req.end()) {
      c->InitRTP();
      c->rtp_handler().SetNegotiationArgs(rtpit->get<std::map<std::string,json>>());
    }
    SDP sdp(client_sdp);
    if (!sdp.Answer(mpmit->get<std::map<std::string, std::string>>(), *c, answer)) {
      logger::error({{"ev","invalid client sdp"},{"sdp",client_sdp},{"reason",answer}});
      return QRPC_EINVAL; // if return from here, c will be freed because no anchor exists
    }
    connections_[std::move(ufrag)] = c;
    c->RegisterCname();
    // generate response
    response.emplace("sdp", std::move(answer));
    if (c->rtp_enabled()) {
      response.emplace("mid_media_path_map",c->rtp_handler().mid_media_path_map());
    }
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