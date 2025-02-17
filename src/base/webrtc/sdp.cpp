#include "base/webrtc/sdp.h"
#include "base/logger.h"
#include "base/string.h"

#include "base/rtp/parameters.h"

#include "RTC/RtpProbationGenerator.hpp"

namespace base {
namespace webrtc {
  int SDP::Offer(const ConnectionFactory::Connection &c,
    const std::string &ufrag, const std::string &pwd, std::string &offer) {
    auto now = qrpc_time_now();
    // string value to the str::Format should be converted to c string like str.c_str()
    // TODO: add sdp for audio and video
    offer = str::Format(R"sdp(v=0
o=- %llu %llu IN IP4 0.0.0.0
s=-
t=0 0
a=group:BUNDLE 0
a=extmap-allow-mixed
a=msid-semantic: WMS
m=application 9 %s/DTLS/SCTP webrtc-datachannel
c=IN IP4 0.0.0.0
a=ice-ufrag:%s
a=ice-pwd:%s
a=ice-options:trickle
a=fingerprint:%s %s
a=setup:active
a=mid:0
a=sctp-port:5000
a=max-message-size:%u
)sdp",
      now, now,
      c.factory().primary_proto().c_str(),
      ufrag.c_str(), pwd.c_str(),
      c.factory().fingerprint_algorithm().c_str(), c.factory().fingerprint().c_str(),
      c.factory().config().send_buffer_size
    );
    return QRPC_OK;
  }
  bool SDP::GetRemoteFingerPrint(const json &section, std::string &answer, RTC::DtlsTransport::Fingerprint &ret) const {
      auto root_fp = find("fingerprint");
      auto fp = root_fp;
      if (fp == end()) {
        fp = section.find("fingerprint");
        if (fp == section.end()) {
          logger::error({{"ev","malform sdp"},{"reason","no fingerprint"}, {"as_json", dump()}});
          // malicious?
          answer = "no fingerprint found";
          ASSERT(false);
          return false;
        }
      }
      auto type = fp->find("type");
      auto hash = fp->find("hash");
      if (type == fp->end() || hash == fp->end()) {
        logger::error({{"ev","malform sdp"},{"reason","no fingerprint type or hash"}, {"as_json", dump()}});
        // malicious?
        answer = "no fingerprint type or hash found";
        ASSERT(false);
        return false;
      }
      auto algoit = RTC::DtlsTransport::GetString2FingerprintAlgorithm().find(*type);
      if (algoit == RTC::DtlsTransport::GetString2FingerprintAlgorithm().end()) {
        answer = "unknown fingerprint algorithm:" + type->get<std::string>();
        ASSERT(false);
        return false;
      }
      ret = {.algorithm = algoit->second, .value = *hash};
      return true;     
  }
  std::vector<Candidate> SDP::Candidates() const {
    std::vector<Candidate> v;
    auto mit = find("media");
    if (mit == end()) {
      ASSERT(false);
      return v;
    }
    for (auto it = mit->begin(); it != mit->end(); ++it) {
      auto protoit = it->find("protocol");
      bool dgram = false;
      if ((*protoit) == "UDP/DTLS/SCTP") {
        dgram = true;
      } else if ((*protoit) == "TCP/DTLS/SCTP") {
      } else {
        QRPC_LOGJ(warn, {{"ev","non SCTP media protocol"}, {"proto", *protoit}});
        continue;
      }
      auto cand = it->find("candidates");
      if (cand == it->end()) {
        QRPC_LOGJ(warn, {{"ev","no candidates value"},{"sdp",dump()}});
        ASSERT(false);
        continue;
      }
      auto uflagit = it->find("iceUfrag");
      if (uflagit == it->end()) {
        QRPC_LOGJ(warn, {{"ev","no ufrag value"},{"sdp",dump()}});
        ASSERT(false);
        continue;
      }
      auto pwdit = it->find("icePwd");
      if (pwdit == it->end()) {
        QRPC_LOGJ(warn, {{"ev","no pwd value"},{"sdp",dump()}});
        ASSERT(false);
        continue;
      }
      for (auto cit = cand->begin(); cit != cand->end(); ++cit) {
        auto hostit = cit->find("ip");
        if (hostit == cit->end()) {
          QRPC_LOGJ(warn, {{"ev","no host value"},{"sdp",dump()}});
          ASSERT(false);
          continue;
        }
        auto portit = cit->find("port");
        if (portit == cit->end()) {
          QRPC_LOGJ(warn, {{"ev","non port valuests"},{"sdp",dump()}});
          ASSERT(false);
          continue;
        }
        auto prioit = cit->find("priority");
        if (prioit == cit->end()) {
          QRPC_LOGJ(warn, {{"ev","no priority value"},{"sdp",dump()}});
          ASSERT(false);
          continue;
        }
        std::string answer;
        RTC::DtlsTransport::Fingerprint fp;
        if (!GetRemoteFingerPrint(*it, answer, fp)) {
          logger::warn({{"ev","failed to get remote fingerprint"},{"reason",answer}});
          continue;
        }
        v.push_back(Candidate(dgram, *hostit, *portit, *uflagit, *pwdit, *prioit, fp));
      }
    }
    return v;
  }
  uint32_t SDP::AssignPriority(uint32_t component_id) {
    // borrow from
    // https://github.com/IIlllII/bitbreeds-webrtc/blob/master/webrtc-signaling/src/main/java/com/bitbreeds/webrtc/signaling/SDPUtil.java#L191
    return 2113929216 + 16776960 + (256 - component_id);
  }

  std::string SDP::CandidatesSDP(const std::string &proto, ConnectionFactory::Connection &c) {
    std::string sdplines;
    auto &l = c.factory().to<Listener>();
    ASSERT(proto == "UDP" || proto == "TCP");
    auto nwport = proto == "UDP" ? l.udp_port() : l.tcp_port();
    size_t idx = 0;
    for (auto &a : c.factory().config().ifaddrs) {
      sdplines += str::Format(
        "a=candidate:0 %u %s %u %s %u typ host\n",
        idx + 1, proto.c_str(), AssignPriority(idx), a.c_str(), nwport
      );
      idx++;
    }
    sdplines += str::Format(R"cands(a=end-of-candidates
a=sctp-port:5000
a=max-message-size:%u)cands",
      c.factory().config().send_buffer_size
    );
    return sdplines;
  }

  bool SDP::FindMediaSection(const std::string &type, json &j) const {
    auto mit = find("media");
    if (mit == end()) {
      ASSERT(false);
      return false;
    }
    for (auto it = mit->begin(); it != mit->end(); ++it) {
      auto tit = it->find("type");
      if (tit == it->end()) {
        // has no type. ignore
        continue;
      }
      if ((*tit) == type) {
        j = json(*it);
        return true;
      }
    }
    return false;
  }

  bool SDP::GenerateSectionAnswer(
    ConnectionFactory::Connection &c, const std::string &proto,
    const rtp::MediaStreamConfig &p, std::string &answer
  ) {
    std::string cname;
    if (p.sender()) {
      if (!p.GenerateCN(cname)) {
        // if returns false, cname contains error message
        answer = cname;
        ASSERT(false);
        return false;
      }
    }
    auto msid = p.media_stream_id();
    if (msid.empty()) {
      answer = "invalid media_path:" + p.media_path;
      ASSERT(false);
      return false;
    }
    // a=msid line is mandatory to avoid safari's error
    // "Media section has more than one track specified with a=ssrc lines which is not supported with Unified Plan"
    answer = str::Format(16 * 1024, R"sdp_section(m=%s %llu %s%s
c=IN IP4 0.0.0.0
a=mid:%s
a=msid:%s %s
a=%s
a=ice-ufrag:%s
a=ice-pwd:%s
a=ice-options:%s
a=fingerprint:%s %s
a=setup:active
%s%s
)sdp_section",
      p.MediaKindName().c_str(), p.network.port, p.RtpProtocol().c_str(), p.Payloads().c_str(),
      p.mid.c_str(),
      msid.c_str(), p.media_stream_track_id().c_str(),
      p.closed ? "inacitive" : "sendrecv",
      c.ice_server().GetUsernameFragment().c_str(),
      c.ice_server().GetPassword().c_str(),
      p.receiver() ? "trickle" : "renomination",
      c.factory().fingerprint_algorithm().c_str(), c.factory().fingerprint().c_str(),
      p.Answer(cname).c_str(),
      CandidatesSDP(proto, c).c_str()
    );
    return true;
  }

  bool SDP::GenerateAnswer(
    ConnectionFactory::Connection &c, const std::string &proto,
    const rtp::MediaStreamConfigs &configs, std::string &answer
  ) {
    auto now = qrpc_time_now();
    auto bundle = std::string("a=group:BUNDLE");
    std::string media_sections, body;
    for (auto &config : configs) {
      bundle += (" " + config.mid);
      if (!GenerateSectionAnswer(c, proto, config, body)) {
        ASSERT(false);
        return false;
      }
      media_sections += body;
    }
    // string value to the str::Format should be converted to c string like str.c_str()
    // a=ice-lite attribute is important for indicating to peer that we are ice-lite mode
    answer = str::Format(16 * 1024, R"sdp(v=0
o=- %llu %llu IN IP4 0.0.0.0
s=-
t=0 0
a=ice-lite
%s
a=msid-semantic: WMS
%s)sdp",
      now, now,
      bundle.c_str(),
      media_sections.c_str()
    ); 
    return true;   
  }  

  bool SDP::AnswerMediaSection(
    const json &section, const std::string &proto,
    const std::map<std::string, std::string> mid_path_map,
    ConnectionFactory::Connection &c,
    rtp::MediaStreamConfig &params,
    std::string &errmsg
  ) const {
    auto tit = section.find("type");
    if (tit == section.end()) {
      errmsg = "section: no value for key 'type'";
      ASSERT(false);
      return false;
    }
    auto media_type = tit->get<std::string>();
    std::string sdplines;
    if (media_type == "application") {
      auto midit = section.find("mid");
      if (midit == section.end()) {
        errmsg = "section: no value for key 'mid'";
        ASSERT(false);
        return false;
      }
      auto portit = section.find("port");
      if (portit == section.end()) {
        errmsg = "rtpmap: no value for key 'port'";
        ASSERT(false);
        return false;
      }
      auto protoit = section.find("protocol");
      if (protoit == section.end()) {
        errmsg = "section: no value for key 'protocol'";
        ASSERT(false);
        return false;
      }
      params.kind = rtp::Parameters::MediaKind::APP;
      // network.port is mandatory
      params.network.port = portit->get<uint16_t>();
      params.rtp_proto = protoit->get<std::string>();
      params.media_path = c.cname() + "/qrpc/app";
      params.mid = c.GenerateMid();
    } else {
      rtp::Capability cap;
      c.InitRTP();
      if (!params.Parse(section, cap, errmsg, &c.rtp_handler())) {
        return false;
      }
      auto pit = mid_path_map.find(params.mid);
      if (pit == mid_path_map.end()) {
        errmsg = "find label from mid = " + params.mid;
        ASSERT(false);
        return false;
      }
      params.media_path = pit->second;
      // assign actual mid and update label map
      params.mid = c.rtp_handler().GenerateMid();
      c.rtp_handler().UpdateMidMediaPathMap(params);
    }
    params.direction = rtp::MediaStreamConfig::Direction::RECV;
    return true;
  }

  bool SDP::Answer(
    const std::map<std::string, std::string> mid_path_map,
    ConnectionFactory::Connection &c, std::string &answer,
    const std::map<rtp::Parameters::MediaKind, rtp::MediaStreamConfig::ControlOptions> *options_map,
    std::map<std::string, rtp::Producer*> *created_producers
  ) const {
    json dsec, asec, vsec;
    auto &section_params = c.media_stream_configs();
    std::string media_sections, proto;
    std::vector<std::string> mids;
    rtp::Producer *p;
    if (FindMediaSection("application", dsec)) {
      // find protocol from SCTP backed transport
      auto protoit = dsec.find("protocol");
      if (protoit == dsec.end()) {
        answer = "there is no value for key 'protocol'";
        QRPC_LOGJ(warn, {{"ev","failed to get remote fingerprint"},{"reason",answer},{"section",dsec}});
        ASSERT(false);
        return false;
      }
      auto sctp_proto = protoit->get<std::string>();
      if (sctp_proto == "UDP/DTLS/SCTP") {
        proto = "UDP";
      } else if (sctp_proto == "TCP/DTLS/SCTP") {
        proto = "TCP";
      } else {
        answer = "non SCTP media protocol";
        QRPC_LOGJ(debug, {{"ev",answer}, {"proto", sctp_proto}});
        return false;
      }
      // protocol found. set remote fingerprint
      RTC::DtlsTransport::Fingerprint fp;
      if (!GetRemoteFingerPrint(dsec, answer, fp)) {
        QRPC_LOGJ(warn, {{"ev","failed to get remote fingerprint"},{"reason",answer}});
        return false;
      }
      c.dtls_transport().SetRemoteFingerprint(fp);
      if (created_producers == nullptr) {
        auto &params = section_params.emplace_back();
        if (!AnswerMediaSection(dsec, proto, mid_path_map, c, params, answer)) {
          QRPC_LOGJ(warn, {{"ev","invalid data channel section"},{"section",dsec}});
          ASSERT(false);
          return false;
        }
      }
    }
    // TODO: should support multiple audio/video streams from same webrtc connection?
    if (FindMediaSection("audio", asec)) {
      auto &params = section_params.emplace_back();
      if (AnswerMediaSection(asec, proto, mid_path_map, c, params, answer)) {
        if (options_map != nullptr) {
          auto it = options_map->find(rtp::Parameters::MediaKind::AUDIO);
          if (it != options_map->end()) {
            params.options = it->second;
          }
        }
        if ((p = c.rtp_handler().Produce(params)) == nullptr) {
          answer = "fail to create audio producer";
          return false;
        }
        if (created_producers != nullptr) {
          (*created_producers)[params.media_path] = p;
        }
      } else {
        QRPC_LOGJ(warn, {{"ev","invalid audio section"},{"section",asec},{"reason",answer}});
        ASSERT(false);
        return false;
      }
    }
    if (FindMediaSection("video", vsec)) {
      auto &params = section_params.emplace_back();
      if (AnswerMediaSection(vsec, proto, mid_path_map, c, params, answer)) {
        if (options_map != nullptr) {
          auto it = options_map->find(rtp::Parameters::MediaKind::VIDEO);
          if (it != options_map->end()) {
            params.options = it->second;
          }
        }
        if ((p = c.rtp_handler().Produce(params)) == nullptr) {
          answer = "fail to create video producer";
          return false;
        }
        if (created_producers != nullptr) {
          (*created_producers)[params.media_path] = p;
        }
      } else {
        QRPC_LOGJ(warn, {{"ev","invalid video section"},{"section",vsec},{"answer",answer}});
        ASSERT(false);
        return false;
      }
    }
    // geneating answer for prodducer
    ASSERT(!proto.empty());
    return GenerateAnswer(c, proto, section_params, answer);
  }
} // namespace webrtc
} // namespace base