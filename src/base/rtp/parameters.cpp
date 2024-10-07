#include "base/rtp/parameters.h"

#include "base/string.h"
#include "base/rtp/handler.h"

#include "FBS/transport.h"

namespace base {
namespace rtp {
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
    RTC::RtpCodecParameters params;
    bool rtx;
  };

  static std::vector<std::string> resilient_codecs = {
    "rtx", // retransmission
    "red", // Redundant encoding Data
    "ulpfec", // Uneven Level Protection Forward Error Correction
  };
  // earlier entry is high priority. TODO: decide best order
  static std::vector<std::string> supported_codecs = {
    "VP8", "VP9", "H264", "opus",
  };

  static uint64_t SelectRtpmap(std::map<uint64_t, RtpMap> &rtpmaps, std::string &answer) {
    size_t candidate_index = -1;
    uint64_t current_candidate = 0;
    for (auto &kv : rtpmaps) {
      if (kv.second.rtx) {
        continue;
      }
      auto it = std::find(supported_codecs.begin(), supported_codecs.end(), kv.second.codec);
      if (it == supported_codecs.end()) {
        // unsupported codec
        QRPC_LOGJ(warn, {{"ev","unsupported codec"},{"codec",kv.second.codec}});
        continue;
      }
      auto index = std::distance(supported_codecs.begin(), it);
      if (candidate_index < 0) {
        candidate_index = index;
        current_candidate = kv.first;
      } else if (index < candidate_index) {
        candidate_index = index;
        current_candidate = kv.first;
      }
      if (candidate_index == 0) {
        return current_candidate;
      }
    }
    return current_candidate;
  }

  RTC::RtpCodecParameters *Parameters::CodecByPayloadType(uint64_t pt) {
    for (auto &c : codecs) {
      if (c.payloadType == pt) {
        return &c;
      }
    }
    ASSERT(false);
    return nullptr;
  }

    std::vector<::flatbuffers::Offset<FBS::RtpParameters::CodecMapping>>
    Parameters::PackCodecMapping(::flatbuffers::FlatBufferBuilder &fbb) const {
    std::vector<::flatbuffers::Offset<FBS::RtpParameters::CodecMapping>> r;
    r.reserve(codecs.size());
    for (auto &c : codecs) {
      r.emplace_back(FBS::RtpParameters::CreateCodecMapping(
        fbb, c.payloadType, c.payloadType
      ));
    }
    return r;
  }
  std::vector<::flatbuffers::Offset<FBS::RtpParameters::EncodingMapping>>
    Parameters::PackEncodingMapping(::flatbuffers::FlatBufferBuilder &fbb) const {
    std::vector<::flatbuffers::Offset<FBS::RtpParameters::EncodingMapping>> r;
    r.reserve(encodings.size());
    auto seed = GenerateSsrc();
    for (auto &e : encodings) {
      r.emplace_back(FBS::RtpParameters::CreateEncodingMappingDirect(
        fbb, e.rid.empty() ? nullptr : e.rid.c_str(), 
        e.ssrc != 0 ? std::optional(e.ssrc) : std::nullopt,
        e.scalabilityMode.empty() ? nullptr : e.scalabilityMode.c_str(),
        seed++
      ));
    }
    return r;
  }
  ::flatbuffers::Offset<FBS::RtpParameters::RtpMapping>
  Parameters::PackRtpMapping(::flatbuffers::FlatBufferBuilder &fbb) const {
    auto c = PackCodecMapping(fbb);
    auto e = PackEncodingMapping(fbb);
    return FBS::RtpParameters::CreateRtpMappingDirect(fbb, &c, &e);
  }
  ::flatbuffers::Offset<FBS::Transport::ProduceRequest>
  Parameters::MakeProduceRequest(
    ::flatbuffers::FlatBufferBuilder &fbb, const std::string &id
  ) const {
    ASSERT(kind != MediaKind::APP);
    return FBS::Transport::CreateProduceRequestDirect(
      fbb, id.c_str(), static_cast<FBS::RtpParameters::MediaKind>(kind),
      FillBuffer(fbb),
      PackRtpMapping(fbb)
    );
  }

  std::optional<RTC::RtpHeaderExtensionUri::Type> Parameters::FromUri(const std::string &uri) {
    static std::map<std::string, RTC::RtpHeaderExtensionUri::Type> map = {
      {"urn:ietf:params:rtp-hdrext:sdes:mid", RTC::RtpHeaderExtensionUri::Type::MID},
      {"urn:ietf:params:rtp-hdrext:sdes:rtp-stream-id", RTC::RtpHeaderExtensionUri::Type::RTP_STREAM_ID},
      {"urn:ietf:params:rtp-hdrext:sdes:repaired-rtp-stream-id", RTC::RtpHeaderExtensionUri::Type::REPAIRED_RTP_STREAM_ID},
      {"http://tools.ietf.org/html/draft-ietf-avtext-framemarking-07", RTC::RtpHeaderExtensionUri::Type::FRAME_MARKING_07},
      {"urn:ietf:params:rtp-hdrext:framemarking", RTC::RtpHeaderExtensionUri::Type::FRAME_MARKING},
      {"urn:ietf:params:rtp-hdrext:ssrc-audio-level", RTC::RtpHeaderExtensionUri::Type::SSRC_AUDIO_LEVEL},
      {"urn:3gpp:video-orientation", RTC::RtpHeaderExtensionUri::Type::VIDEO_ORIENTATION},
      {"urn:ietf:params:rtp-hdrext:toffset", RTC::RtpHeaderExtensionUri::Type::TOFFSET},
      {"http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01", RTC::RtpHeaderExtensionUri::Type::TRANSPORT_WIDE_CC_01},
      {"http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time", RTC::RtpHeaderExtensionUri::Type::ABS_SEND_TIME},
      {"http://www.webrtc.org/experiments/rtp-hdrext/abs-capture-time", RTC::RtpHeaderExtensionUri::Type::ABS_CAPTURE_TIME},
      {"http://www.webrtc.org/experiments/rtp-hdrext/playout-delay", RTC::RtpHeaderExtensionUri::Type::PLAYOUT_DELAY},
    };
    auto it = map.find(uri);
    if (it == map.end()) {
      ASSERT(false);
      return std::nullopt;
    }
    return std::optional(it->second);
  }

  void Parameters::AddEncoding(
    const std::string &rid, uint64_t pt, uint64_t rtxpt, bool dtx,
    const std::string &scalability_mode) {
    RTC::RtpEncodingParameters p;
    p.rid = rid;
    p.scalabilityMode = scalability_mode;
    p.dtx = dtx;
    p.codecPayloadType = pt;
    p.hasCodecPayloadType = pt != 0;
    p.hasRtx = rtxpt != 0; // here, rtx ssrc is unknown
    encodings.push_back(p);
  }
  void Parameters::AddEncoding(uint32_t ssrc, uint64_t pt, uint64_t rtxpt, bool dtx) {
    RTC::RtpEncodingParameters p;
    p.ssrc = ssrc;
    p.dtx = dtx;
    p.codecPayloadType = pt;
    p.hasCodecPayloadType = pt != 0;
    p.hasRtx = rtxpt != 0; // here, rtx ssrc is unknown
    encodings.push_back(p);
  }
  int Parameters::Parse(Handler &h, const json &section, std::string &answer)  {
    auto midit = section.find("mid");
    if (midit == section.end()) {
      answer = "section: no value for key 'mid'";
      ASSERT(false);
      return false;
    }
    auto tyit = section.find("type");
    if (tyit == section.end()) {
      answer = "section: no value for key 'type'";
      ASSERT(false);
      return false;
    }
    auto kind_str = tyit->get<std::string>();
    static std::map<std::string, MediaKind> kind_map = {
      {"audio", MediaKind::AUDIO},
      {"video", MediaKind::VIDEO},
      // {"application", MediaKind::APP}, // should not come here with kind_str == "application"
    };
    auto kindit = kind_map.find(kind_str);
    if (kindit == kind_map.end()) {
      answer = str::Format("section: invalid value for key 'type': %s", kind_str.c_str());
      ASSERT(false);
      return false;
    } else {
      kind = kindit->second;
    }
    mid = midit->get<std::string>();
    auto rtcpit = section.find("rtcp");
    if (rtcpit != section.end()) {
      FIND_OR_RAISE(ait, rtcpit, "address");
      FIND_OR_RAISE(ipvit, rtcpit, "ipVer");
      FIND_OR_RAISE(ntit, rtcpit, "netType");
      FIND_OR_RAISE(portit, rtcpit, "port");
      network.address = ait->get<std::string>();
      network.ip_ver = ipvit->get<uint16_t>();
      network.net_type = ntit->get<std::string>();
      network.port = portit->get<uint16_t>();
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
      auto uri = uit->get<std::string>();
      auto id = vit->get<uint8_t>();
      if (!h.SetExtensionId(id, uri)) {
        continue;
      }
      RTC::RtpHeaderExtensionParameters p;
      p.id = id;
      auto t = FromUri(uri);
      if (t.has_value()) {
        p.type = t.value();
      } else {
        answer = "rtpmap: no uri type for '" + uri + "'";
        ASSERT(false);
        return false;
      }
      headerExtensions.push_back(p);
    }
    // parse fmtp
    std::map<uint64_t, std::string> fmtpmap;
    std::map<uint64_t, uint64_t> rtxmap; // target codec stream pt => rtx pt
    std::map<uint64_t, bool> dtxmap; // target codec stream pt => dtx
    auto fmtpit = section.find("fmtp");
    if (fmtpit != section.end()) {
      for (auto it = fmtpit->begin(); it != fmtpit->end(); it++) {
        FIND_OR_RAISE(cit, it, "config");
        FIND_OR_RAISE(plit, it, "payload");
        auto config = cit->get<std::string>();
        auto pt = plit->get<uint64_t>();
        fmtpmap[pt] = config;
        if (config.find("apt=") == 0) {
          try {
            auto apt = std::stoul(config.substr(4));
            rtxmap[apt] = pt;
          } catch (std::exception &e) {
            answer = str::Format("failed to parse pt [%s]", config.c_str());
            QRPC_LOGJ(warn, {{"ev","failed to parse pt"},{"config",config}});
            ASSERT(false);
            return false;
          }
        } else if (config.find("usedtx=1") != std::string::npos) {
          dtxmap[pt] = true;
        }
      }
    }
    // parse rtpmaps
    std::map<uint64_t, RtpMap> rtpmaps;
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
        auto cit = it->find("codec");
        if (cit == it->end()) {
          answer = "rtpmap: no value for key 'codec'";
          ASSERT(false);
          return false;
        }
        auto codec = cit->get<std::string>();
        auto fit = std::find(supported_codecs.begin(), supported_codecs.end(), codec);
        if (fit == supported_codecs.end()) {
          auto fit2 = std::find(resilient_codecs.begin(), resilient_codecs.end(), codec);
          if (fit2 == resilient_codecs.end()) {
            // really unsupported
            continue;
          }
        }
        auto rateit = it->find("rate");
        if (rateit == it->end()) {
          answer = "rtpmap: no value for key 'rate'";
          ASSERT(false);
          return false;
        }
        auto encit = it->find("encoding");
        try {
          RTC::RtpCodecParameters c;
          c.mimeType.SetMimeType(str::Format("%s/%s", FBS::RtpParameters::EnumNamesMediaKind()[kind], codec.c_str()));
          c.channels = encit == it->end() ? 0 : std::stoul(encit->get<std::string>().c_str());
          c.payloadType = pt;
          c.clockRate = rateit->get<uint64_t>();
          auto sit = std::find(resilient_codecs.begin(), resilient_codecs.end(), codec);
          rtpmaps[pt] = {codec, c, sit != resilient_codecs.end()};
        } catch (std::exception &e) {
          answer = str::Format("rtpmap: parse exception '%s'", e.what());
          ASSERT(false);
          return false;
        }
      }
    }    
    auto selected_pt = SelectRtpmap(rtpmaps, answer);
    if (selected_pt == 0) {
      answer = "no suitable rtpmap";
      ASSERT(false);
      return false;
    }
    auto &rtp = rtpmaps[selected_pt];
    codecs.push_back(rtp.params);
    uint64_t rtx_pt = 0;
    auto rtxit = rtxmap.find(selected_pt);
    auto usedtx = dtxmap.find(selected_pt) != dtxmap.end();
    if (rtxit != rtxmap.end()) {
      rtx_pt = rtxit->second;
      auto rtxrtpit = rtpmaps.find(rtx_pt);
      if (rtxrtpit == rtpmaps.end()) {
        answer = str::Format("rtpmap: rtx pt not found: %llu", rtx_pt);
        ASSERT(false);
        return false;
      }
      codecs.push_back(rtxrtpit->second.params);
    }
    std::vector<uint64_t> pts = { selected_pt, rtx_pt };
    for (int i = 0; i < 2; i++) {
      auto pt = pts[i];
      auto c = CodecByPayloadType(pt);
      if (c == nullptr) {
        answer = str::Format("rtpmap: no codec added for %llu", pt);
        ASSERT(false);
        return false;
      }
      auto fmit = fmtpmap.find(pt);
      if (fmit != fmtpmap.end()) {
        for (auto t : str::Split(fmit->second, ";")) {
          auto kv = str::Split(t, "=");
          if (kv.size() != 2) {
            answer = str::Format("rtpmap: invalid fmtp format: %s", fmit->second.c_str());
            ASSERT(false);
            return false;
          }
          try {
            if (kv[1] == "true" || kv[1] == "false") {
              c->parameters.Add(kv[0], kv[1] == "true"); // boolean
            } else if (kv[1].find('.') != std::string::npos) { 
              try {
                c->parameters.Add(kv[0], std::stod(kv[1])); // double
              } catch (std::exception &) {
                c->parameters.Add(kv[0], std::stoi(kv[1])); // int
              }
            } else {
              c->parameters.Add(kv[0], std::stoi(kv[1])); // int
            }
          } catch (std::exception &) {
            c->parameters.Add(kv[0], kv[1]); // string
          }
        }
      }
    }
    auto rtcpfbit = section.find("rtcpFb");
    if (rtcpfbit != section.end()) {
      for (auto it = rtcpfbit->begin(); it != rtcpfbit->end(); it++) {
        auto plit = it->find("payload");
        if (plit == it->end()) {
          answer = "rtcp-fb: no value for key 'payload'";
          ASSERT(false);
          return false;
        }
        RTC::RtpCodecParameters *c;
        try {
          auto pt = std::stoul(plit->get<std::string>());
          if (std::find(pts.begin(), pts.end(), pt) == pts.end()) {
            continue;
          }
          c = CodecByPayloadType(pt);
          if (c == nullptr) {
            answer = str::Format("rtpmap: no codec added for %llu", pt);
            ASSERT(false);
            return false;
          }
        } catch (std::exception &e) {
          answer = str::Format("rtcp-fb: parse payload value error %s", e.what());
          ASSERT(false);
          return false;
        }
        auto tit = it->find("type");
        if (tit == it->end()) {
          answer = "rtcp-fb: no value for key 'type'";
          ASSERT(false);
          return false;
        }
        auto stit = it->find("subtype");
        RTC::RtcpFeedback fb;
        fb.type = tit->get<std::string>();
        if (stit != it->end()) {
            fb.parameter = stit->get<std::string>();
        }
        c->rtcpFeedback.push_back(fb);
      }
    }
    auto ssrcit = section.find("ssrcs");
    if (ssrcit != section.end()) {
      bool found = false;
      for (auto it = ssrcit->begin(); it != ssrcit->end(); it++) {
        auto ait = it->find("attribute");
        if (ait == it->end()) {
          continue;
        } else if (ait->get<std::string>() == "msid") {
          // store ssrc => track id relation to webrtc connection
          auto idit = it->find("id");
          auto vit = it->find("value");
          auto id = idit->get<uint32_t>();
          auto values = str::Split(vit->get<std::string>(), " ");
          if (values.size() > 1) {
            auto ssrcit = ssrcs.find(id);
            if (ssrcit == ssrcs.end()) {
              ssrcs[id] = {
                .msid = values[0], .track_id = values[1]
              };
            } else {
              ssrcit->second.msid = values[0];
              ssrcit->second.track_id = values[1];
            }
            found = true;
          }
        } else if (ait->get<std::string>() == "cname") {
          auto idit = it->find("id");
          auto vit = it->find("value");
          auto id = idit->get<uint32_t>();
          AddEncoding(id, selected_pt, rtx_pt, usedtx);
          auto ssrcit = ssrcs.find(id);
          if (ssrcit == ssrcs.end()) {
            ssrcs[id] = {
              .cname = vit->get<std::string>()
            };
          } else {
            ssrcit->second.cname = vit->get<std::string>();
          }
        }
      }
      if (!found) {
        answer = str::Format("fail to find ssrc/trackid relation");
        QRPC_LOGJ(warn, {{"ev","failed to parse pt"},{"ssrcs",*ssrcit}});
        ASSERT(false);
        return false;
      }
    }
    auto scit = section.find("simulcast");
    if (scit != section.end()) {
      FIND_OR_RAISE(dit, scit, "dir1");
      FIND_OR_RAISE(lit, scit, "list1");
      auto dir1 = dit->get<std::string>();
      auto &rids = dir1 == "send" ? simulcast.send_rids : simulcast.recv_rids;
      rids = lit->get<std::string>();
      for (auto &rid : str::Split(rids, ";")) {
        auto scalability_mode = h.FindScalabilityMode(rid);
        AddEncoding(rid, selected_pt, rtx_pt, usedtx, scalability_mode);
      }
      auto d2it = scit->find("dir2");
      if (d2it != scit->end()) {
        FIND_OR_RAISE(l2it, scit, "list2");
        auto dir2 = d2it->get<std::string>();
        auto &rids = dir2 == "send" ? simulcast.send_rids : simulcast.recv_rids;
        rids = l2it->get<std::string>();
        for (auto &rid : str::Split(rids, ";")) {
          auto scalability_mode = h.FindScalabilityMode(rid);
          AddEncoding(rid, selected_pt, rtx_pt, usedtx, scalability_mode);
        }
      }
    }
    return QRPC_OK;
  }
  std::string Parameters::Answer() const {
		// std::string mid;
		// std::vector<RtpCodecParameters> codecs;
		// std::vector<RtpEncodingParameters> encodings;
		// std::vector<RtpHeaderExtensionParameters> headerExtensions;
		// RtcpParameters rtcp;
		// bool hasRtcp{ false };
    std::string sdplines;
    if (kind == MediaKind::APP) {
      return sdplines;
    }
    if (network.port != 0 && network.ip_ver != 0) {
      sdplines += str::Format(
        "a=rtcp:%llu %s IP%llu %s\na=rtcp-mux\na=rtcp-rsize",
        network.port,
        network.address.c_str(),
        network.ip_ver,
        network.net_type.c_str()
      );
    }
    auto &hs = headerExtensions;
    for (size_t i = 0; i < hs.size(); i++) {
      sdplines += str::Format("\na=extmap:%u %s", hs[i].id, hs[i].type);
    }
    for (size_t i = 0; i < codecs.size(); i++) {
      auto &c = codecs[i];
      auto mimeTypeVec = str::Split(c.mimeType.ToString(), "/");
      if (c.channels > 1) {
        sdplines += str::Format(
          "\na=rtpmap:%llu %s/%llu/%llu",
          c.payloadType,
          mimeTypeVec[1].c_str(),
          c.clockRate,
          c.channels
        );
      } else {
        sdplines += str::Format(
          "\na=rtpmap:%llu %s/%llu",
          c.payloadType,
          mimeTypeVec[1].c_str(),
          c.clockRate
        );
      }
      for (auto &rtcpfb : c.rtcpFeedback) {
        if (rtcpfb.parameter.empty()) {
          sdplines += str::Format(
            "\na=rtcp-fb:%llu %s",
            c.payloadType, rtcpfb.type.c_str()
          );
        } else {
          sdplines += str::Format(
            "\na=rtcp-fb:%llu %s %s",
            c.payloadType, rtcpfb.type.c_str(), rtcpfb.parameter.c_str()
          );
        }
      }
      std::string paramsline;
      for (auto &e : c.parameters.Entries()) {
        if (!paramsline.empty()) {
          paramsline += ";";
        }
        if (e.second.type == RTC::Parameters::Value::Type::BOOLEAN) {
          paramsline = str::Format(
            "%s=%s", e.first.c_str(), e.second.booleanValue ? "true" : "false"
          );
        } else if (e.second.type == RTC::Parameters::Value::Type::DOUBLE) {
          paramsline = str::Format(
            "%s=%llf", e.first.c_str(), e.second.doubleValue
          );
        } else if (e.second.type == RTC::Parameters::Value::Type::INTEGER) {
          paramsline = str::Format(
            "%s=%d", e.first.c_str(), e.second.integerValue
          );
        } else if (e.second.type == RTC::Parameters::Value::Type::STRING) {
          paramsline = str::Format(
            "%s=%s", e.first.c_str(), e.second.stringValue.c_str()
          );
        } else if (e.second.type == RTC::Parameters::Value::Type::ARRAY_OF_INTEGERS) {
          // TODO: how format string?
          ASSERT(false);
        } else {
          ASSERT(false);
        }
      }
      sdplines += str::Format(
        "\na=fmtp:%llu %s\na=fmtp:%llu x-google-start-bitrate=10000",
        c.payloadType, paramsline.c_str(), c.payloadType
      );
    }
    std::string scline = "\na=simulcast:";
    bool has_sc = false;
    if (!simulcast.recv_rids.empty()) {
      for (auto s : str::Split(simulcast.recv_rids, ";")) {
        sdplines += str::Format("\na=rid:%s send", s.c_str());
      }
      scline += str::Format(
        "send %s", simulcast.recv_rids.c_str()
      );
      has_sc = true;
    }
    if (!simulcast.send_rids.empty()) {
      for (auto s : str::Split(simulcast.send_rids, ";")) {
        sdplines += str::Format("\na=rid:%s recv", s.c_str());
      }
      scline += str::Format(
        " recv %s", simulcast.send_rids.c_str()
      );
      has_sc = true;
    }
    if (has_sc) {
      sdplines += scline;
    }
    sdplines += "\n";
    return sdplines;
  }
}
}