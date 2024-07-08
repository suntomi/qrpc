#include "base/webrtc/sdp.h"
#include "base/logger.h"
#include "base/string.h"

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
  bool SDP::GetRemoteFingerPrint(const json::const_iterator &it, std::string &answer, DtlsTransport::Fingerprint &ret) const {
      auto root_fp = find("fingerprint");
      auto fp = root_fp;
      if (fp == end()) {
        fp = it->find("fingerprint");
        if (fp == it->end()) {
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
      auto algo = DtlsTransport::GetFingerprintAlgorithm(*type);
      if (algo == DtlsTransport::FingerprintAlgorithm::NONE) {
        answer = "unknown fingerprint algorithm:" + type->get<std::string>();
        ASSERT(false);
        return false;
      }
      ret = {.algorithm = algo, .value = *hash};
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
        DtlsTransport::Fingerprint fp;
        if (!GetRemoteFingerPrint(it, answer, fp)) {
          logger::warn({{"ev","failed to get remote fingerprint"},{"reason",answer}});
          continue;
        }
        v.push_back(Candidate(dgram, *hostit, *portit, *uflagit, *pwdit, *prioit, fp));
      }
    }
    return v;
  }
  bool SDP::Answer(ConnectionFactory::Connection &c, const SDP &client_sdp, std::string &answer) const {
    try {
      auto mit = find("media");
      if (mit == end()) {
        answer = "no media found";
        ASSERT(false);
        return false;
      }
      // firefox contains fingerprint in root section, not in media
      for (auto it = mit->begin(); it != mit->end(); ++it) {
        auto proto = it->find("protocol");
        if (proto == it->end()) {
          logger::warn({{"ev","media does not have protocol"}, {"media", *it}});
          ASSERT(false);
          continue;
        }
        if ((*proto) == "UDP/DTLS/SCTP") {
          // answer udp port
          if (!AnswerAs("UDP", client_sdp, c, answer)) {
            return false;
          }
        } else if ((*proto) == "TCP/DTLS/SCTP") {
          // answer tcp port
          if (!AnswerAs("TCP", client_sdp, c, answer)) {
            return false;
          }
        } else {
          logger::debug({{"ev","non SCTP media protocol"}, {"proto", *proto}});
          continue;
        }
        // protocol found. set remote fingerprint
        DtlsTransport::Fingerprint fp;
        if (!GetRemoteFingerPrint(it, answer, fp)) {
          logger::warn({{"ev","failed to get remote fingerprint"},{"reason",answer}});
          continue;
        }
        c.dtls_transport().SetRemoteFingerprint(fp);
        return true;
      }
      answer = "no data channel media found";
      return false;
    } catch (std::exception &e) {
      QRPC_LOGJ(error, {{"ev","sdp parse error"},{"what",e.what()}});
      ASSERT(false);
      return false;
    }
  }
  uint32_t SDP::AssignPriority(uint32_t component_id) const {
    // borrow from
    // https://github.com/IIlllII/bitbreeds-webrtc/blob/master/webrtc-signaling/src/main/java/com/bitbreeds/webrtc/signaling/SDPUtil.java#L191
    return 2113929216 + 16776960 + (256 - component_id);
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

  bool SDP::AnswerMediaSection(
    const json &section, const std::string &proto,
    const ConnectionFactory::Connection &c,
    std::string &answer, std::string &mid
  ) const {
    auto tit = section.find("type");
    if (tit == section.end()) {
      answer = "section: no value for key 'type'";
      ASSERT(false);
      return false;
    }
    auto media_type = tit->get<std::string>();
    std::string rtpmap, payloads, candidates;
    if (media_type == "application") {
      auto &l = c.factory().to<Listener>();
      auto nwport = proto == "UDP" ? l.udp_port() : l.tcp_port();
      size_t idx = 0;
      for (auto &a : c.factory().config().ifaddrs) {
        candidates += str::Format(
          "a=candidate:0 %u %s %u %s %u typ host\n",
          idx + 1, proto.c_str(), AssignPriority(idx), a.c_str(), nwport
        );
        idx++;
      }
      candidates += str::Format(R"cands(a=end-of-candidates
a=sctp-port:5000
a=max-message-size:%u)cands",
        c.factory().config().send_buffer_size
      );
      payloads = "webrtc-datachannel";
    } else {
      auto rtpmit = section.find("rtp");
      if (rtpmit == section.end()) {
        answer = "rtpmap: no value for key 'rtp'";
        ASSERT(false);
        return false;
      } else {
        for (auto it = rtpmit->begin(); it != rtpmit->end(); it++) {
          auto plit = it->find("payload");
          if (plit == it->end()) {
            answer = "rtpmap: no value for key 'payload'";
            ASSERT(false);
            return false;
          }
          auto pl = plit->get<std::string>();
          payloads += pl + " ";
          auto cit = it->find("codec");
          if (cit == it->end()) {
            answer = "rtpmap: no value for key 'codec'";
            ASSERT(false);
            return false;
           }
          auto rateit = it->find("rate");
          if (rateit == it->end()) {
            answer = "rtpmap: no value for key 'rate'";
            ASSERT(false);
            return false;
           }
          auto encit = it->find("encoding");
          if (encit != it->end()) {
            rtpmap += str::Format(
              "a=rtpmap:%s %s/%s/%s\n",
              pl.c_str(),
              cit->get<std::string>().c_str(),
              rateit->get<std::string>().c_str(),
              encit->get<std::string>().c_str()
            );
          } else {
            rtpmap += str::Format(
              "a=rtpmap:%s %s/%s\n",
              pl.c_str(),
              cit->get<std::string>().c_str(),
              rateit->get<std::string>().c_str()
            );
          }
        }
        rtpmap += "a=rtcp-mux";
      }
    }
    auto midit = section.find("mid");
    if (midit == section.end()) {
      answer = "section: no value for key 'mid'";
      ASSERT(false);
      return false;
    }
    mid = midit->get<std::string>();
    auto portit = section.find("port");
    if (portit == section.end()) {
      answer = "section: no value for key 'port'";
      ASSERT(false);
      return false;
    }
    auto protoit = section.find("protocol");
    if (protoit == section.end()) {
      answer = "section: no value for key 'protocol'";
      ASSERT(false);
      return false;
    }
    answer = str::Format(4096, R"sdp_section(m=%s %llu %s %s
c=IN IP4 0.0.0.0
a=mid:%s
a=sendrecv
%s
a=ice-lite
a=ice-ufrag:%s
a=ice-pwd:%s
a=fingerprint:%s %s
a=setup:active
)sdp_section",
      media_type.c_str(), portit->get<uint64_t>(), protoit->get<std::string>().c_str(), payloads.c_str(),
      mid.c_str(),
      media_type == "application" ? candidates.c_str() : rtpmap.c_str(),
      c.ice_server().GetUsernameFragment().c_str(),
      c.ice_server().GetPassword().c_str(),
      c.factory().fingerprint_algorithm().c_str(), c.factory().fingerprint().c_str()
    );
    return true;
  }

  bool SDP::AnswerAs(
    const std::string &proto, const SDP &client_sdp, const ConnectionFactory::Connection &c,
    std::string &answer) const {
    auto now = qrpc_time_now();
    auto bundle = std::string("a=group:BUNDLE");
    json dsec, asec, vsec;
    std::string mid, media_sections, section_answer;
    if (client_sdp.FindMediaSection("application", dsec)) {
      if (AnswerMediaSection(dsec, proto, c, section_answer, mid)) {
        media_sections += section_answer;
        bundle += str::Format(" %s", mid.c_str());
      } else {
        QRPC_LOGJ(warn, {{"ev","invalid data channel section"},{"section",dsec}});
        answer = section_answer;
        ASSERT(false);
        return false;
      }
    }
    if (client_sdp.FindMediaSection("audio", asec)) {
      if (AnswerMediaSection(asec, proto, c, section_answer, mid)) {
        media_sections += section_answer;
        bundle += str::Format(" %s", mid.c_str());
      } else {
        QRPC_LOGJ(warn, {{"ev","invalid audio section"},{"section",asec}});
        answer = section_answer;
        ASSERT(false);
        return false;
      }
    }
    if (client_sdp.FindMediaSection("video", vsec)) {
      if (AnswerMediaSection(vsec, proto, c, section_answer, mid)) {
        media_sections += section_answer;
        bundle += str::Format(" %s", mid.c_str());
      } else {
        QRPC_LOGJ(warn, {{"ev","invalid video section"},{"section",vsec}});
        answer = section_answer;
        ASSERT(false);
        return false;
      }
    }
    // string value to the str::Format should be converted to c string like str.c_str()
    answer = str::Format(4096, R"sdp(v=0
o=- %llu %llu IN IP4 0.0.0.0
s=-
t=0 0
%s
a=msid-semantic: WMS
%s)sdp",
      now, now,
      bundle.c_str(),
      media_sections.c_str()
    );
    return true;
  }
} // namespace webrtc
} // namespace base