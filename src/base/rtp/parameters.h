#pragma once

#include "base/crypto.h"
#include "base/string.h"

#include <FBS/transport.h>
#include "RTC/RtpDictionaries.hpp"

#include <optional>

namespace base {
namespace rtp {
  class Handler;
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
    Parameters() : RTC::RtpParameters(), ssrc_seed(GenerateSsrc()) {}
    bool Parse(Handler &h, const json &section, std::string &answer);
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
    inline const std::string &RtpProtocol() const { return rtp_proto; }
    inline std::string MediaKindName() const { return FromMediaKind(kind); }
    RTC::RtpCodecParameters *CodecByPayloadType(uint64_t pt);
    void AddEncoding(
      const std::string &rid, uint64_t pt, uint64_t rtxpt, bool dtx,
      const std::string &scalability_mode);
    void AddEncoding(uint32_t ssrc, uint32_t rtx_ssrc, 
      uint64_t pt, uint64_t rtxpt, bool dtx);
    static inline uint32_t GenerateSsrc() {
      return random::gen(100000000, 900000000);
    }
  public:
    ::flatbuffers::Offset<FBS::Transport::ProduceRequest>
    MakeProduceRequest(::flatbuffers::FlatBufferBuilder &fbb, const std::string &id) const;
    std::vector<::flatbuffers::Offset<FBS::RtpParameters::RtpEncodingParameters>>
    PackConsumableEncodings(::flatbuffers::FlatBufferBuilder &fbb) const;
    void GetGeneratedSsrc(std::vector<uint32_t> &generated_ssrcs) const;
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
    MediaKind kind;             // affect sdp geeneration
    NetworkParameters network;  // affect sdp generation
    std::string rtp_proto;      // affect sdp generation
    uint32_t ssrc_seed;         // affect consumer sdp generation only
    std::map<uint32_t, SsrcParameter> ssrcs;                  // affect producer sdp generation only
    SimulcastParameter simulcast;                             // affect producer sdp generation only
    std::vector<RTC::RtpCodecParameters> codec_capabilities;  // affect nothing for sdp generation
  };
}
}