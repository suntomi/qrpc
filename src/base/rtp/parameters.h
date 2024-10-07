#pragma once

#include "base/crypto.h"

#include <FBS/transport.h>
#include "RTC/RtpDictionaries.hpp"

#include <optional>

namespace base {
namespace rtp {
  class Handler;
  class Capability {
  public:
    enum MediaKind {
      AUDIO,
      VIDEO,
    };
    struct RtcpFeedback {

    };
    struct Codec {
      MediaKind kind;
      std::string mime_type;
      uint64_t preferred_payload_type;
      uint32_t clock_rate;
      uint32_t channels;
      std::map<std::string, std::string> parameters;
      std::vector<RtcpFeedback> rtcp_fb;
    };
  public:
    Capability() {}
  public:
    std::vector<Codec> codecs;
  };
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
    Parameters() : RTC::RtpParameters() {}
    int Parse(Handler &h, const json &section, std::string &answer);
    std::string Answer() const;
    RTC::RtpCodecParameters *CodecByPayloadType(uint64_t pt);
    void AddEncoding(
      const std::string &rid, uint64_t pt, uint64_t rtxpt, bool dtx,
      const std::string &scalability_mode
    );
    void AddEncoding(uint32_t ssrc, uint64_t pt, uint64_t rtxpt, bool dtx);
    static inline uint32_t GenerateSsrc() {
      return random::gen(100000000, 900000000);
    }
  public:
    ::flatbuffers::Offset<FBS::Transport::ProduceRequest>
    MakeProduceRequest(::flatbuffers::FlatBufferBuilder &fbb, const std::string &id) const;
    static std::optional<RTC::RtpHeaderExtensionUri::Type> FromUri(const std::string &uri);
  protected:
    std::vector<::flatbuffers::Offset<FBS::RtpParameters::CodecMapping>>
    PackCodecMapping(::flatbuffers::FlatBufferBuilder &fbb) const;
    std::vector<::flatbuffers::Offset<FBS::RtpParameters::EncodingMapping>>
    PackEncodingMapping(::flatbuffers::FlatBufferBuilder &fbb) const;
    ::flatbuffers::Offset<FBS::RtpParameters::RtpMapping>
    PackRtpMapping(::flatbuffers::FlatBufferBuilder &fbb) const;  
  public:
    MediaKind kind;
    NetworkParameters network;
    std::map<uint32_t, SsrcParameter> ssrcs;
    SimulcastParameter simulcast;
  };
}
}