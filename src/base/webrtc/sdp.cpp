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
  bool SDP::GetRemoteFingerPrint(const json &section, std::string &answer, DtlsTransport::Fingerprint &ret) const {
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
        if (!GetRemoteFingerPrint(*it, answer, fp)) {
          logger::warn({{"ev","failed to get remote fingerprint"},{"reason",answer}});
          continue;
        }
        v.push_back(Candidate(dgram, *hostit, *portit, *uflagit, *pwdit, *prioit, fp));
      }
    }
    return v;
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

  #define FIND_OR_RAISE(__name, __it, __key) \
    auto __name = __it->find(__key); \
    if (__name == __it->end()) { \
      answer = "no value for key '" __key "'"; \
      QRPC_LOGJ(error, {{"ev","sdp parse error"},{"reason",answer},{"section",*__it}}); \
      ASSERT(false); \
      return false; \
    }

  struct RtpMap {
    std::string codec;
    bool used;
    std::string sdpline;
  };

  static std::vector<std::string> resilient_codecs = {
    "rtx", // retransmission
    "red", // Redundant encoding Data
    "ulpfec", // Uneven Level Protection Forward Error Correction
  };

  static std::string SelectRtpmap(std::map<std::string, RtpMap> &rtpmaps) {
    // earlier entry is high priority. TODO: decide best order
    static std::vector<std::string> priority = {"VP9", "VP8", "H264"};
    size_t candidate_index = -1;
    std::string current_candidate;
    for (auto &kv : rtpmaps) {
      if (kv.second.used) {
        // already used
        continue;
      }
      auto it = std::find(priority.begin(), priority.end(), kv.second.codec);
      if (it == priority.end()) {
        auto sit = std::find(resilient_codecs.begin(), resilient_codecs.end(), kv.second.codec);
        if (sit == resilient_codecs.end()) {
          // unsupported codec
          QRPC_LOGJ(warn, {{"ev","unsupported codec"},{"codec",kv.second.codec}});
          ASSERT(false);
        }
        continue;
      }
      auto index = std::distance(priority.begin(), it);             
      if (candidate_index < 0) {
        candidate_index = index;
        current_candidate = kv.first;
      } else if (index < candidate_index) {
        candidate_index = index;
        current_candidate = kv.first;
      }
      if (candidate_index == 0) {
        rtpmaps[current_candidate].used = true;
        return current_candidate;
      }
    }
    rtpmaps[current_candidate].used = true;
    return current_candidate;
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
      payloads = " webrtc-datachannel";
    } else {
      auto rtcpit = section.find("rtcp");
      if (rtcpit != section.end()) {
        FIND_OR_RAISE(ait, rtcpit, "address");
        FIND_OR_RAISE(ipvit, rtcpit, "ipVer");
        FIND_OR_RAISE(ntit, rtcpit, "netType");
        FIND_OR_RAISE(portit, rtcpit, "port");
        rtpmap += str::Format(
          "a=rtcp:%llu %s IP%llu %s\na=rtcp-mux",
          portit->get<uint64_t>(),
          ntit->get<std::string>().c_str(),
          ipvit->get<uint64_t>(),
          ait->get<std::string>().c_str()
        );
      }
      auto extmit = section.find("ext");
      if (extmit == section.end()) {
        answer = "rtpmap: no value for key 'ext'";
        ASSERT(false);
        return false;
      }
      for (auto it = extmit->begin(); it != extmit->end(); it++) {
        FIND_OR_RAISE(uit, it, "uri");
        FIND_OR_RAISE(vit, it, "value");
        rtpmap += str::Format(
          "\na=extmap:%llu %s",
          vit->get<uint64_t>(),
          uit->get<std::string>().c_str()
        );
      }
      // parse fmtp
      std::map<uint64_t, std::string> fmtpmap;
      std::map<uint64_t, uint64_t> rtxmap; // rtx pt => target codec stream pt
      auto fmtpit = section.find("fmtp");
      if (fmtpit != section.end()) {
        for (auto it = fmtpit->begin(); it != fmtpit->end(); it++) {
          FIND_OR_RAISE(cit, it, "config");
          FIND_OR_RAISE(plit, it, "payload");
          auto config = cit->get<std::string>();
          auto pt = plit->get<uint64_t>();
          fmtpmap[pt] = config;
          if (config.find("apt=") == 0) {
            auto apt = std::stoul(config.substr(4));
            rtxmap[pt] = apt;
          }
        }
      }
      // parse rtpmaps
      std::map<std::string, RtpMap> rtpmaps;
      auto rtpmit = section.find("rtp");
      if (rtpmit == section.end()) {
        answer = "rtpmap: no value for key 'rtp'";
        ASSERT(false);
        return false;
      } else {
        for (auto it = rtpmit->begin(); it != rtpmit->end(); it++) {
          auto ptit = it->find("payload");
          if (ptit == it->end()) {
            answer = "rtpmap: no value for key 'payload'";
            ASSERT(false);
            return false;
          }
          auto pt = ptit->get<uint64_t>();
          auto ptstr = std::to_string(pt);
          auto cit = it->find("codec");
          if (cit == it->end()) {
            answer = "rtpmap: no value for key 'codec'";
            ASSERT(false);
            return false;
          }
          auto codec = cit->get<std::string>();
          static std::vector<std::string> supported_codecs = {
            "opus", "H264", "VP8", "VP9",
          };
          auto fit = std::find(supported_codecs.begin(), supported_codecs.end(), codec);
          if (fit == supported_codecs.end()) {
            auto fit2 = std::find(resilient_codecs.begin(), resilient_codecs.end(), codec);
            if (fit2 == resilient_codecs.end()) {
              // really unsupported
              continue;
            }
          }
          payloads += (" " + ptstr);
          auto rateit = it->find("rate");
          if (rateit == it->end()) {
            answer = "rtpmap: no value for key 'rate'";
            ASSERT(false);
            return false;
          }
          auto encit = it->find("encoding");
          std::string sdpline;
          if (encit != it->end()) {
            sdpline = str::Format(
              "\na=rtpmap:%llu %s/%llu/%s",
              pt,
              cit->get<std::string>().c_str(),
              rateit->get<uint64_t>(),
              encit->get<std::string>().c_str()
            );
          } else {
            sdpline = str::Format(
              "\na=rtpmap:%llu %s/%llu",
              pt,
              cit->get<std::string>().c_str(),
              rateit->get<uint64_t>()
            );
          }
          rtpmaps[ptstr] = {codec, false, sdpline};
        }
      }
      for (auto &kv : rtpmaps) {
        if (kv.second.codec == "rtx") {
          auto rtxit = rtxmap.find(std::stoul(kv.first));
          if (rtxit != rtxmap.end()) {
            if (rtpmaps.find(std::to_string(rtxit->second)) == rtpmaps.end()) {
              QRPC_LOGJ(info, {{"ev", "rtpmap: rtx target stream filtered"}, {"target", rtxit->second}, {"rtx", kv.first}});
              continue;
            }
          }
        }
        rtpmap += kv.second.sdpline;
      }
      for (auto &kv : fmtpmap) {
        if (rtpmaps.find(std::to_string(kv.first)) == rtpmaps.end()) {
          continue;
        }
        auto rtxit = rtxmap.find(kv.first);
        if (rtxit != rtxmap.end()) {
          if (rtpmaps.find(std::to_string(rtxit->second)) == rtpmaps.end()) {
            QRPC_LOGJ(info, {{"ev", "fmtp: rtx target stream filtered"},{"target", rtxit->second},{"rtx", kv.first}});
            continue;
          }
        }
        rtpmap += str::Format("\na=fmtp:%llu %s", kv.first, kv.second.c_str());
      }
      auto rtcpfbit = section.find("rtcpFb");
      if (rtcpfbit != section.end()) {
        // put \n to top of line because string from rtp element of SDP does not have \n at last of string.
        // and entire rtpmap string should not include \n at last of string. (see below str::Format call)
        rtpmap += "\na=rtcp-rsize";
        for (auto it = rtcpfbit->begin(); it != rtcpfbit->end(); it++) {
          auto plit = it->find("payload");
          if (plit == it->end()) {
            answer = "rtcp-fb: no value for key 'payload'";
            ASSERT(false);
            return false;
          }
          auto pl = plit->get<std::string>();
          if (rtpmaps.find(pl) == rtpmaps.end()) {
            continue;
          } else {
            auto rtxit = rtxmap.find(std::stoul(pl));
            if (rtxit != rtxmap.end()) {
              if (rtpmaps.find(std::to_string(rtxit->second)) == rtpmaps.end()) {
                QRPC_LOGJ(info, {{"ev", "rtcp-fb: rtx target stream filtered"},{"target", rtxit->second},{"rtx", pl}});
                continue;
              }
            }
          }
          auto tit = it->find("type");
          if (tit == it->end()) {
            answer = "rtcp-fb: no value for key 'type'";
            ASSERT(false);
            return false;
          }
          auto stit = it->find("subtype");
          if (stit == it->end()) {
            rtpmap += str::Format(
              "\na=rtcp-fb:%s %s",
              pl.c_str(),
              tit->get<std::string>().c_str()
            );
          } else {
            rtpmap += str::Format(
              "\na=rtcp-fb:%s %s %s",
              pl.c_str(),
              tit->get<std::string>().c_str(),
              stit->get<std::string>().c_str()
            );
          }
        }
      }
      auto scit = section.find("simulcast");
      if (scit != section.end()) {
        FIND_OR_RAISE(dit, scit, "dir1");
        FIND_OR_RAISE(lit, scit, "list1");
        auto dir1 = dit->get<std::string>();
        for (auto s : str::Split(lit->get<std::string>(), ";")) {
          if (dir1 == "send") {
            auto pt = SelectRtpmap(rtpmaps);
            rtpmap += str::Format("\na=rid:%s recv pt=%s", s.c_str(), pt.c_str());
          } else {
            rtpmap += str::Format("\na=rid:%s send", s.c_str());
          }
        }
        auto d2it = scit->find("dir2");
        if (d2it != scit->end()) {
          FIND_OR_RAISE(l2it, scit, "list2");
          auto dir2 = d2it->get<std::string>();
          for (auto s : str::Split(l2it->get<std::string>(), ";")) {
            if (dir2 == "send") {
              auto pt = SelectRtpmap(rtpmaps);
              rtpmap += str::Format("\na=rid:%s recv pt=%s", s.c_str(), pt.c_str());
            } else {
              rtpmap += str::Format("\na=rid:%s send", s.c_str());
            }
          }
        }
        rtpmap += str::Format(
          "\na=simulcast:%s rid=%s",
          dit->get<std::string>() == "send" ? "recv" : "send",
          lit->get<std::string>().c_str()
        );
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
    answer = str::Format(16384, R"sdp_section(m=%s %llu %s%s
c=IN IP4 0.0.0.0
a=mid:%s
a=sendrecv
a=ice-lite
a=ice-ufrag:%s
a=ice-pwd:%s
a=fingerprint:%s %s
a=setup:active
%s
)sdp_section",
      media_type.c_str(), portit->get<uint64_t>(), protoit->get<std::string>().c_str(), payloads.c_str(),
      mid.c_str(),
      c.ice_server().GetUsernameFragment().c_str(),
      c.ice_server().GetPassword().c_str(),
      c.factory().fingerprint_algorithm().c_str(), c.factory().fingerprint().c_str(),
      media_type == "application" ? candidates.c_str() : rtpmap.c_str()
    );
    return true;
  }

  bool SDP::Answer(ConnectionFactory::Connection &c, std::string &answer) const {
    auto now = qrpc_time_now();
    auto bundle = std::string("a=group:BUNDLE");
    json dsec, asec, vsec;
    std::string mid, media_sections, section_answer, proto;
    if (FindMediaSection("audio", asec)) {
      if (AnswerMediaSection(asec, proto, c, section_answer, mid)) {
        media_sections += section_answer;
        bundle += str::Format(" %s", mid.c_str());
      } else {
        answer = section_answer;
        QRPC_LOGJ(warn, {{"ev","invalid audio section"},{"section",asec},{"reason",answer}});
        ASSERT(false);
        return false;
      }
    }
    if (FindMediaSection("video", vsec)) {
      if (AnswerMediaSection(vsec, proto, c, section_answer, mid)) {
        media_sections += section_answer;
        bundle += str::Format(" %s", mid.c_str());
      } else {
        answer = section_answer;
        QRPC_LOGJ(warn, {{"ev","invalid video section"},{"section",vsec},{"answer",answer}});
        ASSERT(false);
        return false;
      }
    }
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
      DtlsTransport::Fingerprint fp;
      if (!GetRemoteFingerPrint(dsec, answer, fp)) {
        QRPC_LOGJ(warn, {{"ev","failed to get remote fingerprint"},{"reason",answer}});
        return false;
      }
      c.dtls_transport().SetRemoteFingerprint(fp);
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
    // string value to the str::Format should be converted to c string like str.c_str()
    answer = str::Format(16384, R"sdp(v=0
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