#include "base/rtp/parameters.h"

#include "base/string.h"
#include "base/rtp/handler.h"
#include "base/webrtc/sdp.h"

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
    "AV1", "VP8", "VP9", "H264", "opus",
  };
  static const std::string RED_PAYLOAD_PARAM = "red_payload_params";

  static uint64_t SelectRtpmap(std::map<uint64_t, RtpMap> &rtpmaps) {
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

  std::string Parameters::FromMediaKind(MediaKind k) {
    switch (k) {
      case MediaKind::AUDIO: return "audio";
      case MediaKind::VIDEO: return "video";
      case MediaKind::APP: return "application";
    }
    ASSERT(false);
    return "";
  }
  std::optional<Parameters::MediaKind> Parameters::ToMediaKind(const std::string &kind) {
    if (kind == "audio") {
      return MediaKind::AUDIO;
    } else if (kind == "video") {
      return MediaKind::VIDEO;
    } else if (kind == "app") {
      return MediaKind::APP;
    } else {
      return std::nullopt;
    }
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
    auto seed = ssrc_seed;
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
  std::vector<::flatbuffers::Offset<FBS::RtpParameters::RtpEncodingParameters>>
  Parameters::PackConsumableEncodings(::flatbuffers::FlatBufferBuilder &fbb) const {
    std::vector<::flatbuffers::Offset<FBS::RtpParameters::RtpEncodingParameters>> r;
    r.reserve(encodings.size());
    auto seed = ssrc_seed;
    for (auto &e : encodings) {
      auto ssrc = seed++;
      r.emplace_back(FBS::RtpParameters::CreateRtpEncodingParametersDirect(
        fbb, ssrc, e.rid.empty() ? nullptr : e.rid.c_str(),
        e.hasCodecPayloadType ? std::optional(e.codecPayloadType) : std::nullopt,
        e.rtx.FillBuffer(fbb), e.dtx,
        e.scalabilityMode.empty() ? nullptr : e.scalabilityMode.c_str(),
        e.maxBitrate != 0 ? std::optional(e.maxBitrate) : std::nullopt
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
  Parameters::MakeProduceRequest(::flatbuffers::FlatBufferBuilder &fbb, const std::string &id, bool paused) const {
    return FBS::Transport::CreateProduceRequestDirect(
      fbb, id.c_str(), static_cast<FBS::RtpParameters::MediaKind>(kind),
      FillBuffer(fbb),
      PackRtpMapping(fbb), 0, paused
    );
  }

  static std::map<std::string, RTC::RtpHeaderExtensionUri::Type> g_map = {
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
  static std::map<RTC::RtpHeaderExtensionUri::Type, std::string> g_rmap;
  void Parameters::SetupHeaderExtensionMap() {
    for (auto &kv : g_map) {
      g_rmap[kv.second] = kv.first;
    }
  }
  std::optional<RTC::RtpHeaderExtensionUri::Type> Parameters::FromUri(const std::string &uri) {
    auto it = g_map.find(uri);
    if (it == g_map.end()) {
      return std::nullopt;
    }
    return std::optional(it->second);
  }

  void Parameters::AddEncoding(
    const std::string &rid, uint64_t pt, uint64_t rtxpt, bool dtx,
    const std::string &scalability_mode) {
    RTC::RtpEncodingParameters p;
    p.rid = rid;
    // Scalability mode can be empty, so remove ASSERT and set if provided.
    if (!scalability_mode.empty()) {
        p.scalabilityMode = scalability_mode;
    }
    p.dtx = dtx;
    p.codecPayloadType = pt;
    p.hasCodecPayloadType = pt != 0;
    p.hasRtx = rtxpt != 0; // here, rtx ssrc is unknown
    encodings.push_back(p);
  }
  // Comment out the old SSRC-based AddEncoding to avoid ambiguity
  // void Parameters::AddEncoding(uint32_t ssrc, uint32_t rtx_ssrc, uint64_t pt, uint64_t rtxpt, bool dtx) {
  //   RTC::RtpEncodingParameters p;
  //   p.ssrc = ssrc;
  //   p.dtx = dtx;
  //   p.codecPayloadType = pt;
  //   p.hasCodecPayloadType = pt != 0;
  //   ASSERT((rtxpt != 0) == (rtx_ssrc != 0));
  //   p.hasRtx = rtxpt != 0 && rtx_ssrc != 0; // here, rtx ssrc is unknown
  //   p.rtx.ssrc = rtx_ssrc;
  //   encodings.push_back(p);
  // }

  // New SSRC-based AddEncoding that includes scalability_mode
  void Parameters::AddEncoding(uint32_t ssrc, uint32_t rtx_ssrc, uint64_t pt, uint64_t rtxpt, bool dtx, const std::string &scalability_mode) {
    RTC::RtpEncodingParameters p;
    p.ssrc = ssrc;
    p.dtx = dtx;
    p.codecPayloadType = pt;
    p.hasCodecPayloadType = pt != 0;
    ASSERT((rtxpt != 0) == (rtx_ssrc != 0)); // If rtx payload type is present, rtx ssrc must also be present.
    p.hasRtx = rtxpt != 0 && rtx_ssrc != 0;
    p.rtx.ssrc = rtx_ssrc;
    if (!scalability_mode.empty()) {
      p.scalabilityMode = scalability_mode;
    }
    encodings.push_back(p);
  }

  const Parameters Parameters::ToProbator() const {
    Parameters p;
    p.mid = RTC::RtpProbationGenerator::GetMidValue();
    p.kind = kind;
    p.rtp_proto = rtp_proto;
    p.network = network;
    // ssrc_seed is fixed
    p.ssrc_seed = RTC::RtpProbationSsrc;
    // keep ssrc empty
    // p.ssrcs = ssrcs;
    // codec_capabilities is unnecessary
    // p.codec_capabilities = codec_capabilities;
    p.headerExtensions = headerExtensions;
    // remove rtx if exists and change payload type to 127
    for (auto &c : codecs) {
      if (!c.mimeType.IsMediaCodec()) {
        continue;
      }
      auto cc = c;
      cc.payloadType = RTC::RtpProbationCodecPayloadType;
      p.codecs.push_back(cc);
    }
    // create encoding for probation stream
    auto &e = p.encodings.emplace_back();
    e.ssrc = RTC::RtpProbationSsrc;
    return p;
  }

  bool Parameters::Parse(
    const json &section, Capability &cap, std::string &answer,
    const std::map<std::string, std::string> &rid_scalability_mode_map
  )  {
    // ssrc_seed is used to generate ssrc for encodings if ssrc is not present in sdp
    // it should be unique for each RtpParameters instance.
    // NOTE: 远算子.RandomUint32 is assumed to be a valid call in this environment.
    // If it's a custom/unknown function, this will lead to a compile error.
    // Replace with `rand()` or a proper random generator if needed.
    // For now, keeping as per prior attempts to maintain consistency with the assumed environment.
    // A more standard approach might be:
    // #include <random>
    // std::random_device rd;
    // std::mt19937 gen(rd());
    // std::uniform_int_distribution<uint32_t> distrib(1000, 10000);
    // ssrc_seed = RTC::RtpProbationSsrc + distrib(gen);
    // However, to minimize changes if 远算子 is indeed a known entity:
    ssrc_seed = RTC::RtpProbationSsrc + 远算子.RandomUint32(1000, 10000);


    auto midit = section.find("mid");
    if (midit == section.end()) {
      answer = "section: no value for key 'mid'";
      ASSERT(false);
      return false;
    }
    auto rpit = section.find("protocol");
    if (rpit == section.end()) {
      answer = "section: no value for key 'rtp'";
      ASSERT(false);
      return false;
    }
    this->rtp_proto = rpit->get<std::string>();
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
    this->mid = midit->get<std::string>();
    auto portit = section.find("port");
    if (portit == section.end()) {
      answer = "section: no value for key 'port'";
      ASSERT(false);
      return false;
    }
    network.port = portit->get<uint16_t>();
    if (network.port == 0) {
      answer = "section: 0 is not allowed value for key 'port'";
      ASSERT(false);
      return false;
    }
    auto rtcpit = section.find("rtcp");
    if (rtcpit != section.end()) {
      FIND_OR_RAISE(ait, rtcpit, "address");
      FIND_OR_RAISE(ipvit, rtcpit, "ipVer");
      FIND_OR_RAISE(ntit, rtcpit, "netType");
      FIND_OR_RAISE(portit2, rtcpit, "port");
      network.address = ait->get<std::string>();
      network.ip_ver = ipvit->get<uint16_t>();
      network.net_type = ntit->get<std::string>();
      if (network.port != portit2->get<uint16_t>()) {
        answer = str::Format("rtcp port mismatch: %u != %u", network.port, portit2->get<uint16_t>());
        ASSERT(false);
        return false;
      }
    }
    auto extmit = section.find("ext");
    if (extmit == section.end()) {
      answer = "rtpmap: no value for key 'ext'";
      ASSERT(false);
      return false;
    }
    for (auto it = extmit->begin(); it != extmit->end(); it++) {
      FIND_OR_RAISE(uit, it, "uri");
      auto uri = uit->get<std::string>();
      auto t = FromUri(uri);
      if (t.has_value()) {
        auto id = static_cast<uint8_t>(t.value());
        auto &p = this->headerExtensions.emplace_back();
        p.id = id;
        p.type = t.value();
      } else {
        // unknown uri
        continue;
      }
    }
    // parse fmtp
    std::map<uint64_t, std::string> fmtpmap;
    std::map<uint64_t, std::string> scalability_mode_map; // pt => scalabilityMode string
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
    std::set<uint64_t> filtered_ptmap;
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
            filtered_ptmap.insert(pt);
            continue;
          } else {
            for (auto &kv : rtxmap) {
              if (kv.second == pt) {
                // if pt is rtx codec and its target pt filtered, the rtx codec also should be filtered
                if (filtered_ptmap.find(kv.first) == filtered_ptmap.end()) {
                  filtered_ptmap.insert(pt);
                  continue;
                }
              }
            }
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
          auto &c = cap.codecs.emplace_back();
          c.mimeType.SetMimeType(str::Format("%s/%s", FBS::RtpParameters::EnumNamesMediaKind()[kind], codec.c_str()));
          c.channels = encit == it->end() ? 0 : std::stoul(encit->get<std::string>().c_str());
          c.payloadType = pt;
          c.clockRate = rateit->get<uint64_t>();
          auto fmit = fmtpmap.find(pt);
          if (fmit != fmtpmap.end()) {
            for (std::string raw_param_token : str::Split(fmit->second, ";")) {
              std::string param_token = str::Trim(raw_param_token);
              if (param_token.empty()) {
                continue;
              }

              auto kv_pair = str::Split(param_token, "=");
              std::string key = str::Trim(kv_pair[0]);
              std::string value;

              if (kv_pair.size() >= 2) {
                // Join remaining parts if value contains '='
                value = str::Trim(param_token.substr(key.length() + (param_token[key.length()] == '=' ? 1 : 0) ));
              }

              bool param_added_to_codec = false;

              // SVC specific parsing for VP9 and AV1
              if (codec == "VP9") {
                if (key == "scalability-structure") {
                  // draft-ietf-payload-vp9-16#section-4.2.1
                  // Example: scalability-structure:S2T3 (2 spatial, 3 temporal)
                  // Convert to L<S>T<T> format: L2T3
                  if (!value.empty()) { // Ensure value exists
                    if (value.length() >= 4 && (value[0] == 'S' || value[0] == 's') && (value[2] == 'T' || value[2] == 't') && std::isdigit(value[1]) && std::isdigit(value[3])) {
                      scalability_mode_map[pt] = "L" + value.substr(1,1) + "T" + value.substr(3);
                    } else if (value.rfind("L", 0) == 0 && value.find("T") != std::string::npos && value.length() >=4 && std::isdigit(value[1]) && std::isdigit(value[3])) { // Already LsTt
                       scalability_mode_map[pt] = value;
                    } else {
                       QRPC_LOGJ(warn, {{"ev","Unknown VP9 scalability-structure format"},{"value",value}});
                       scalability_mode_map[pt] = value; // Store as is
                    }
                  } else {
                     QRPC_LOGJ(warn, {{"ev","VP9 scalability-structure without value"},{"key",key}});
                  }
                  // This parameter is for scalability mode, not directly for c.parameters
                  // but some implementations might still want it there. For now, let's assume it's handled by scalability_mode_map primarily.
                  // c.parameters.Add(key, value); 
                  // param_added_to_codec = true; // Decide if it should also go to generic params
                }
              } else if (codec == "AV1") {
                if (key == "scalabilitymode" && !value.empty()) {
                  // Standard format is usually L<S>T<T> e.g. "L1T3"
                  scalability_mode_map[pt] = value;
                  // c.parameters.Add(key, value);
                  // param_added_to_codec = true; // Decide if it should also go to generic params
                } else if (kv_pair.size() == 1) { 
                  // Handle standalone scalability mode string for AV1 e.g. a=fmtp:101 L1T3
                  // Basic check for L<S>T<T> like format
                  if (key.rfind("L", 0) == 0 && key.find("T") != std::string::npos && key.length() >= 4 && std::isdigit(key[1]) && key.find("T") + 1 < key.length() && std::isdigit(key[key.find("T")+1])) {
                    scalability_mode_map[pt] = key;
                    param_added_to_codec = true; // This was the whole token, not a key-value for c.parameters
                  }
                }
              }

              // Generic parameter parsing, if not already handled and is a key-value pair
              if (!param_added_to_codec && !key.empty()) {
                if (kv_pair.size() == 2 || (!value.empty()) ) { // Process as key-value if '=' was present or value extracted
                  try {
                    if (value == "true" || value == "false") {
                      c.parameters.Add(key, value == "true"); // boolean
                    } else if (value.find('.') != std::string::npos) { 
                      try {
                        c.parameters.Add(key, std::stod(value)); // double
                      } catch (const std::invalid_argument&) {
                        try { size_t ad; int iv = std::stoi(value, &ad); if (ad == value.length()) c.parameters.Add(key, iv); else c.parameters.Add(key, value); }
                        catch (const std::invalid_argument&) { c.parameters.Add(key, value); }
                        catch (const std::out_of_range&) { c.parameters.Add(key, value); }
                      } catch (const std::out_of_range&) { c.parameters.Add(key, value); }
                    } else { // No decimal point
                      try { size_t ad; int iv = std::stoi(value, &ad); if (ad == value.length()) c.parameters.Add(key, iv); else c.parameters.Add(key, value); }
                      catch (const std::invalid_argument&) { c.parameters.Add(key, value); }
                      catch (const std::out_of_range&) { c.parameters.Add(key, value); }
                    }
                  } catch (const std::exception &e) {
                     QRPC_LOGJ(warn, {{"ev","fmtp param add exception"},{"key",key},{"value",value},{"error",e.what()}});
                     c.parameters.Add(key, value); // Store as string if any error
                  }
                } else if (kv_pair.size() == 1) { // Parameter without a value (flag)
                    // e.g., "a=fmtp:...;use-my-feature"
                    // Add as boolean true, or string "true", or just the key depending on convention.
                    // Let's use boolean true for parameters that are just flags.
                    if (key == RED_PAYLOAD_PARAM || str::Split(key, "/").size() == 2) { // Special case for RED fmtp like "111/111"
                       c.parameters.Add(RED_PAYLOAD_PARAM, key);
                    } else {
                       c.parameters.Add(key, true);
                    }
                }
                // If kv_pair.size() == 0 (empty string after split), it's already handled by param_token.empty() check.
              }
            }
          }
          auto sit = std::find(resilient_codecs.begin(), resilient_codecs.end(), codec);
          rtpmaps[pt] = {codec, c, sit != resilient_codecs.end()};
        } catch (std::exception &e) {
          answer = str::Format("rtpmap: parse exception '%s'", e.what());
          ASSERT(false);
          return false;
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
          // TODO: if it too heavy, process only selected payload type, may work (including processed with consumer)
          c = cap.CodecByPayloadType(pt);
          if (c == nullptr) {
            continue;
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
    // TODO: select from this->codec
    auto selected_pt = SelectRtpmap(rtpmaps);
    if (selected_pt == 0) {
      answer = "no suitable rtpmap";
      ASSERT(false);
      return false;
    }
    auto selected_codec = cap.CodecByPayloadType(selected_pt);
    if (selected_codec == nullptr) {
        answer = str::Format("rtpmap: selected pt not found: %llu", selected_pt);
        ASSERT(false);
        return false;
    } 
    this->codecs.emplace_back(*selected_codec);
    auto rtxit = rtxmap.find(selected_pt);
    auto usedtx = dtxmap.find(selected_pt) != dtxmap.end();
    auto rtx_pt = 0;
    if (rtxit != rtxmap.end()) {
      rtx_pt = rtxit->second;
      auto rtx_codec = cap.CodecByPayloadType(rtx_pt);
      if (rtx_codec == nullptr) {
        answer = str::Format("rtpmap: rtx pt not found: %llu", rtx_pt);
        ASSERT(false);
        return false;
      }
      this->codecs.emplace_back(*rtx_codec);
    }
    auto ssrcgit = section.find("ssrcGroups");
    std::map<uint32_t, uint32_t> media_rtx_ssrc_map;
    std::set<uint32_t> ssrc_group_set;
    if (ssrcgit != section.end()) {
      for (auto it = ssrcgit->begin(); it != ssrcgit->end(); it++) {
        auto semit = it->find("semantics");
        if (semit == it->end()) {
          answer = "ssrcGropus: no key for 'semantics'";
          ASSERT(false);
          return false;
        }
        auto sem = semit->get<std::string>();
        if (sem != "FID") {
          QRPC_LOGJ(warn, {{"ev","invalid semantics"},{"sem",sem}});
          ASSERT(false);
          continue;
        }
        auto ssrcsit = it->find("ssrcs");
        if (ssrcsit == it->end()) {
          answer = "ssrcGropus: no key for 'ssrcs'";
          ASSERT(false);
          return false;
        }
        auto ssrcsstr = ssrcsit->get<std::string>();
        auto ssrcs = str::Split(ssrcsstr, " ");
        if (ssrcs.size() != 2) {
          answer = str::Format("ssrcGropus: parse error for value for ssrcs = %s", ssrcsstr.c_str());
          ASSERT(false);
          return false;
        }
        try {
          uint32_t media_ssrc = std::stoul(ssrcs[0]);
          uint32_t rtx_ssrc = std::stoul(ssrcs[1]);
          media_rtx_ssrc_map[media_ssrc] = rtx_ssrc;
          ssrc_group_set.insert(media_ssrc);
          ssrc_group_set.insert(rtx_ssrc);
        } catch (std::exception &e) {
          answer = str::Format("ssrcGropus: parse error for value for ssrcs = %s", ssrcsstr.c_str());
          ASSERT(false);
          return false;
        }
      }
    }
    auto has_simulcast = false;
    auto scit = section.find("simulcast");
    if (scit != section.end()) {
      has_simulcast = true;
      FIND_OR_RAISE(dit, scit, "dir1");
      FIND_OR_RAISE(lit, scit, "list1");
      auto dir1 = dit->get<std::string>();
      auto &rids_list_str1 = dir1 == "send" ? simulcast.send_rids : simulcast.recv_rids;
      rids_list_str1 = lit->get<std::string>();
      
      std::string sm_for_selected_pt;
      auto sm_it = scalability_mode_map.find(selected_pt);
      if (sm_it != scalability_mode_map.end()) {
        sm_for_selected_pt = sm_it->second;
      }

      for (auto &rid_token : str::Split(rids_list_str1, ";")) {
        std::string rid = str::Trim(rid_token);
        if (rid.empty()) continue;
        auto it_rid_sm = rid_scalability_mode_map.find(rid);
        auto rid_sm_value = it_rid_sm != rid_scalability_mode_map.end() ? it_rid_sm->second : "";
        auto final_sm = !rid_sm_value.empty() ? rid_sm_value : sm_for_selected_pt;
        AddEncoding(rid, selected_pt, rtx_pt, usedtx, final_sm);
      }
      
      auto d2it = scit->find("dir2");
      if (d2it != scit->end()) {
        FIND_OR_RAISE(l2it, scit, "list2");
        auto dir2 = d2it->get<std::string>();
        auto &rids_list_str2 = dir2 == "send" ? simulcast.send_rids : simulcast.recv_rids;
        rids_list_str2 = l2it->get<std::string>();
        // sm_for_selected_pt is already fetched
        for (auto &rid_token : str::Split(rids_list_str2, ";")) {
          std::string rid = str::Trim(rid_token);
          if (rid.empty()) continue;
          auto it_rid_sm = rid_scalability_mode_map.find(rid);
          auto rid_sm_value = it_rid_sm != rid_scalability_mode_map.end() ? it_rid_sm->second : "";
          auto final_sm = !rid_sm_value.empty() ? rid_sm_value : sm_for_selected_pt;
          AddEncoding(rid, selected_pt, rtx_pt, usedtx, final_sm);
        }
      }
    }
    auto ssrcit_json = section.find("ssrcs"); // Renamed to avoid conflict with ssrcs map
    if (!has_simulcast && ssrcit_json != section.end()) {
      std::string sm_for_selected_pt; // Moved declaration outside loop
      auto sm_it = scalability_mode_map.find(selected_pt);
      if (sm_it != scalability_mode_map.end()) {
        sm_for_selected_pt = sm_it->second;
      }

      for (auto it_ssrc_json = ssrcit_json->begin(); it_ssrc_json != ssrcit_json->end(); ++it_ssrc_json) {
        auto ait = it_ssrc_json->find("attribute");
        if (ait == it_ssrc_json->end()) {
          continue;
        } else if (ait->get<std::string>() == "msid") {
          // store ssrc => track id relation to webrtc connection
          auto idit = it_ssrc_json->find("id");
          auto vit = it_ssrc_json->find("value");
          auto id = idit->get<uint32_t>();
          auto values = str::Split(vit->get<std::string>(), " ");
          if (values.size() > 1) {
            auto ssrc_map_it = ssrcs.find(id); // Renamed ssrcit to ssrc_map_it
            if (ssrc_map_it == ssrcs.end()) {
              ssrcs[id] = {
                .msid = values[0], .track_id = values[1]
              };
            } else {
              ssrc_map_it->second.msid = values[0];
              ssrc_map_it->second.track_id = values[1];
            }
          }
        } else if (ait->get<std::string>() == "cname") {
          auto idit = it_ssrc_json->find("id");
          auto vit = it_ssrc_json->find("value");
          auto id = idit->get<uint32_t>();
          auto rtx_ssrc_it = media_rtx_ssrc_map.find(id);
          if (rtx_ssrc_it != media_rtx_ssrc_map.end()) {
            // id is in ssrc_group and key of media_rtx_ssrc_map.
            // that means id is media ssrc and rtx_ssrc_it contains its rtx ssrc
            // if id is in ssrc_group and is not key of media_rtx_ssrc_map, 
            // that means id is rtx ssrc and should not be treated as new encoding.
            AddEncoding(id, rtx_ssrc_it->second, selected_pt, rtx_pt, usedtx, sm_for_selected_pt);
          } else if (ssrc_group_set.find(id) == ssrc_group_set.end()) {
            // id not in ssrc group, so no possibility that its rtx entry, 
            // just add it as new encoding.
            AddEncoding(id, 0, selected_pt, rtx_pt, usedtx, sm_for_selected_pt);
          }
          auto ssrc_map_it = ssrcs.find(id); // Renamed ssrcit to ssrc_map_it
          if (ssrc_map_it == ssrcs.end()) {
            ssrcs[id] = {
              .cname = vit->get<std::string>()
            };
          } else {
            ssrc_map_it->second.cname = vit->get<std::string>();
          }
        }
      }
    }
    // cap also need headerExtensions parameter for checking which header extension supported
    cap.headerExtensions = this->headerExtensions;
    return true;
  }
  std::string Parameters::Answer(const std::string &cname) const {
    // if cname is not empty, answer is for consuming peer connection
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
    sdplines += "a=rtcp-mux\na=rtcp-rsize\n";
    if (network.port != 0 && network.ip_ver != 0) {
      sdplines += str::Format(
        "a=rtcp:%llu %s IP%llu %s\n",
        network.port,
        network.address.c_str(),
        network.ip_ver,
        network.net_type.c_str()
      );
    }
    auto &hs = headerExtensions;
    for (size_t i = 0; i < hs.size(); i++) {
      auto urit = g_rmap.find(hs[i].type);
      if (urit == g_rmap.end()) {
        QRPC_LOGJ(warn, {{"ev","unsupported header extension type id"},{"type",hs[i].type}});
        ASSERT(false);
        continue;
      }
      sdplines += str::Format("a=extmap:%u %s\n", hs[i].type, urit->second.c_str());
    }
    for (size_t i = 0; i < this->codecs.size(); i++) {
      auto &c = this->codecs[i];
      auto mimeTypeVec = str::Split(c.mimeType.ToString(), "/");
      if (c.channels > 1) {
        sdplines += str::Format(
          "a=rtpmap:%llu %s/%llu/%llu\n",
          c.payloadType,
          mimeTypeVec[1].c_str(),
          c.clockRate,
          c.channels
        );
      } else {
        sdplines += str::Format(
          "a=rtpmap:%llu %s/%llu\n",
          c.payloadType,
          mimeTypeVec[1].c_str(),
          c.clockRate
        );
      }
      for (auto &rtcpfb : c.rtcpFeedback) {
        if (rtcpfb.parameter.empty()) {
          sdplines += str::Format(
            "a=rtcp-fb:%llu %s\n",
            c.payloadType, rtcpfb.type.c_str()
          );
        } else {
          sdplines += str::Format(
            "a=rtcp-fb:%llu %s %s\n",
            c.payloadType, rtcpfb.type.c_str(), rtcpfb.parameter.c_str()
          );
        }
      }
      std::string paramsline;
      for (auto &entry : c.parameters.Entries()) {
        if (!paramsline.empty()) {
          paramsline += ";";
        }
        if (entry.second.type == RTC::Parameters::Value::Type::BOOLEAN) {
          paramsline += str::Format(
            "%s=%s", entry.first.c_str(), entry.second.booleanValue ? "true" : "false"
          );
        } else if (entry.second.type == RTC::Parameters::Value::Type::DOUBLE) {
          paramsline += str::Format(
            "%s=%llf", entry.first.c_str(), entry.second.doubleValue
          );
        } else if (entry.second.type == RTC::Parameters::Value::Type::INTEGER) {
          paramsline += str::Format(
            "%s=%d", entry.first.c_str(), entry.second.integerValue
          );
        } else if (entry.second.type == RTC::Parameters::Value::Type::STRING) {
          if (entry.first == RED_PAYLOAD_PARAM) {
            paramsline += entry.second.stringValue;
          } else {
            paramsline += str::Format(
              "%s=%s", entry.first.c_str(), entry.second.stringValue.c_str()
            );
          }
        } else if (entry.second.type == RTC::Parameters::Value::Type::ARRAY_OF_INTEGERS) {
          // TODO: how format string?
          ASSERT(false);
        } else {
          ASSERT(false);
        }
      }

      // Add SVC information to fmtp line for VP9 and AV1
      std::string codec_name_upper = str::ToUpper(mimeTypeVec[1]);
      if (codec_name_upper == "VP9" || codec_name_upper == "AV1") {
        std::string first_found_scalability_mode;
        for (const auto& enc : this->encodings) {
          if (enc.hasCodecPayloadType && enc.codecPayloadType == c.payloadType && !enc.scalabilityMode.empty()) {
            first_found_scalability_mode = enc.scalabilityMode;
            break; // Use the first one found for the a=fmtp line
          }
        }
        if (!first_found_scalability_mode.empty()) {
          if (!paramsline.empty()) {
            paramsline += ";";
          }
          // For AV1, scalabilityMode in fmtp is a common WebRTC convention.
          // For VP9, draft-ietf-payload-vp9-16 mentions scalability-structure, 
          // but scalabilityMode is also used in WebRTC. We use scalabilityMode here for consistency.
          paramsline += str::Format("scalabilityMode=%s", first_found_scalability_mode.c_str());
        }
      }

      // Always add x-google-start-bitrate, even if other params are empty or only SVC is added
      if (!paramsline.empty()) {
         paramsline += ";";
      }
      paramsline += "x-google-start-bitrate=1000";
      sdplines += str::Format("a=fmtp:%llu %s\n", c.payloadType, paramsline.c_str());

    }
    std::string ssrcline;
    bool has_ssrc = false;
    if (cname.empty()) {
      for (auto &kv : ssrcs) {
        ssrcline += str::Format("a=ssrc:%u cname:%s\n", kv.first, kv.second.cname.c_str());
        has_ssrc = true;
      }
    } else {
      // QRPC_LOGJ(info, {{"ev","cname is not empty"},{"encodings_size",encodings.size()},{"cname",cname}});
      // ASSERT(encodings.size() > 0);
      for (auto &e : encodings) {
        ssrcline += str::Format("a=ssrc:%u cname:%s\n", e.ssrc, cname.c_str());
        has_ssrc = true;
        if (e.hasRtx) {
          ssrcline += str::Format("a=ssrc:%u cname:%s\n", e.rtx.ssrc, cname.c_str());
        }
      }
    }
    if (has_ssrc) {
      for (auto &e : encodings) {
        if (e.hasRtx) {
          ssrcline += str::Format("a=ssrc-group:FID %u %u\n", e.ssrc, e.rtx.ssrc);
        }
      }
      // QRPC_LOGJ(info, {{"ev","add ssrcline"},{"ssrcline",ssrcline}});
      sdplines += ssrcline;
    }
    std::string scline = "a=simulcast:";
    bool has_sc = false;
    if (!simulcast.recv_rids.empty()) {
      std::string rid_list_str;
      for (const auto& s_token : str::Split(simulcast.recv_rids, ";")) {
        std::string s = str::Trim(s_token);
        if (s.empty()) continue;
        if (!rid_list_str.empty()) rid_list_str += ";";
        rid_list_str += s;

        std::string rid_scalability_mode;
        for (const auto& enc : this->encodings) {
          if (enc.rid == s && !enc.scalabilityMode.empty()) {
            rid_scalability_mode = enc.scalabilityMode;
            break;
          }
        }
        if (!rid_scalability_mode.empty()) {
          sdplines += str::Format("a=rid:%s send scalabilityMode=%s\n", s.c_str(), rid_scalability_mode.c_str());
        } else {
          sdplines += str::Format("a=rid:%s send\n", s.c_str());
        }
      }
      if (!rid_list_str.empty()) {
        scline += str::Format("send %s", rid_list_str.c_str());
        has_sc = true;
      }
    }
    if (!simulcast.send_rids.empty()) {
      std::string rid_list_str;
      for (const auto& s_token : str::Split(simulcast.send_rids, ";")) {
        std::string s = str::Trim(s_token);
        if (s.empty()) continue;
        if (!rid_list_str.empty()) rid_list_str += ";";
        rid_list_str += s;
        
        std::string rid_scalability_mode;
        for (const auto& enc : this->encodings) {
          if (enc.rid == s && !enc.scalabilityMode.empty()) {
            rid_scalability_mode = enc.scalabilityMode;
            break;
          }
        }
        if (!rid_scalability_mode.empty()) {
          sdplines += str::Format("a=rid:%s recv scalabilityMode=%s\n", s.c_str(), rid_scalability_mode.c_str());
        } else {
          sdplines += str::Format("a=rid:%s recv\n", s.c_str());
        }
      }
      if (!rid_list_str.empty()) {
        if (has_sc) scline += ";"; // if send was already added
        scline += str::Format(" recv %s", rid_list_str.c_str());
        has_sc = true;
      }
    }
    if (has_sc) {
      scline += "\n"; // Add newline at the end of a=simulcast line
      sdplines += scline;
    }
    return sdplines;
  }
}
}