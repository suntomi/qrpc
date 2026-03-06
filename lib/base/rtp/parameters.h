#pragma once

#include "base/crypto.h"
#include "base/string.h"

#include <FBS/transport.h>
#include "RTC/RtpDictionaries.hpp"

#include <optional>

namespace base {
namespace rtp {
  class Handler;
  class Capability;
  class Parameters : public RTC::RtpParameters {
  public:
    struct NetworkParameters {
      std::string address, net_type;
      uint16_t ip_ver{0}, port{0};
    };
    struct SsrcParameter {
      std::string cname, msid, track_id;
    };
    struct SimulcastParameter {
      std::string send_rids, recv_rids;
    };
    enum MediaKind {
      AUDIO = static_cast<int>(FBS::RtpParameters::MediaKind::AUDIO),
      VIDEO = static_cast<int>(FBS::RtpParameters::MediaKind::VIDEO),
      APP = static_cast<int>(FBS::RtpParameters::MediaKind::MAX) + 1,
    };
  public:
    constexpr static std::string_view AUTOGEN_RID_PREFIX = "r";
    Parameters() : RTC::RtpParameters(), ssrc_seed(GenerateSsrc()) {}
    bool Set(MediaKind kind, const qrpc_media_params_t &c, uint32_t &rid_seed);
    bool Parse(const json &section, Capability &cap, std::string &answer,
      const std::map<std::string, std::string> &rid_scalability_mode_map = {});
    static bool ParseFmtp(
      const std::string &fmtp_params, RTC::RtpCodecParameters &codec, std::string &answer);
    std::string Answer(const std::string &cname = "") const;
    std::string Payloads() const {
      if (kind == rtp::Parameters::MediaKind::APP) {
        return " webrtc-datachannel";
      }
      std::string payloads;
      for (auto &c : codecs) {
        payloads += str::Format(" %llu", c.payloadType);
      }
      return payloads;
    }
    const Parameters ToProbator() const;
    static std::string FromMediaKind(MediaKind k);
    static inline std::string FromMediaKind(RTC::Media::Kind k) {
      return FromMediaKind(static_cast<MediaKind>(static_cast<int>(k)));
    }
    static std::optional<MediaKind> ToMediaKind(const std::string &kind);
    static std::string MakeMediaPath(const std::string &base_path, MediaKind kind) {
      return base_path + "/" + FromMediaKind(kind);
    }
    inline const std::string &RtpProtocol() const { return rtp_proto; }
    inline std::string MediaKindName() const { return FromMediaKind(kind); }
    static inline bool SetMimeTypeToCodec(RTC::RtpCodecParameters &codec, const std::string &mime_type) {
      try {
        codec.mimeType.SetMimeType(mime_type);
        return true;
      } catch (const std::exception &e) {
        return false;
      }
    }
    bool AddEncoding(
      const std::string &rid, uint64_t pt, uint64_t rtxpt, bool dtx,
      const std::string &scalability_mode);
    bool AddEncoding(uint32_t ssrc, uint32_t rtx_ssrc, 
      uint64_t pt, uint64_t rtxpt, bool dtx);
    bool GetEncoding(uint32_t ssrc, qrpc_media_encoding_t &encoding) const;
    inline bool FixSsrc(uint32_t old_ssrc, uint32_t new_ssrc) {
			if (!ReplaceEncodings(encodings, old_ssrc, new_ssrc)) {
        ASSERT(false);
				return false;
			}
			auto ssrcsit = this->ssrcs.find(old_ssrc);
			if (ssrcsit == this->ssrcs.end()) {
        ASSERT(false);
				return false;
			} else {
				this->ssrcs[new_ssrc] = ssrcsit->second;
        this->ssrcs.erase(ssrcsit);
			}
      return true;
    }
    static inline bool ReplaceEncodings(
      std::vector<RTC::RtpEncodingParameters> &encodings, uint32_t old_ssrc, uint32_t new_ssrc
    ) {
        auto it = std::find_if(
          encodings.begin(), encodings.end(),
          [ssrc = old_ssrc](const auto &e) { return e.ssrc == ssrc; }
        );
        if (it == encodings.end()) {
          ASSERT(false);
          return false;
        }
        it->ssrc = new_ssrc;
        return true;
    }    
    static inline uint32_t GenerateSsrc() {
      return random::gen(100000000, 900000000);
    }
  public:
    ::flatbuffers::Offset<FBS::Transport::ProduceRequest>
    MakeProduceRequest(::flatbuffers::FlatBufferBuilder &fbb, const std::string &id, bool paused) const;
    std::vector<::flatbuffers::Offset<FBS::RtpParameters::RtpEncodingParameters>>
    PackConsumableEncodings(::flatbuffers::FlatBufferBuilder &fbb) const;
    static std::optional<RTC::RtpHeaderExtensionUri::Type> FromUri(const std::string &uri);
    static void SetupHeaderExtensionMap();
  protected:
    std::vector<::flatbuffers::Offset<FBS::RtpParameters::CodecMapping>>
    PackCodecMapping(::flatbuffers::FlatBufferBuilder &fbb) const;
    std::vector<::flatbuffers::Offset<FBS::RtpParameters::EncodingMapping>>
    PackEncodingMapping(::flatbuffers::FlatBufferBuilder &fbb) const;
    ::flatbuffers::Offset<FBS::RtpParameters::RtpMapping>
    PackRtpMapping(::flatbuffers::FlatBufferBuilder &fbb) const;  
  public:
    MediaKind kind;             // affect sdp geeneration (eg. video, audio)
    NetworkParameters network;  // affect sdp generation (eg. 9 of m=media 9)
    std::string rtp_proto;      // affect sdp generation (eg. UDP/TLS/RTP/SAVPF)
    uint32_t ssrc_seed;         // affect consumer sdp generation only
    std::map<uint32_t, SsrcParameter> ssrcs;                  // affect producer sdp generation only
    SimulcastParameter simulcast;                             // affect producer sdp generation only
  };
}
}