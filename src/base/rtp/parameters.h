#pragma once

#include "base/crypto.h"

#include "RTC/RtpDictionaries.hpp"

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
    json &&ToJson() const;
    json &&ToMapping() const;
  protected:
    json &&CodecsToJson() const;
    json &&ExtensionsToJson() const;
    json &&EncodingsToJson() const;
    json &&RtcpToJson() const;
    json &&CodecMappingToJson() const;
    json &&EncodingMappingToJson() const;
  public:
    std::string media_type; // video/audio/application
    NetworkParameters network;
    std::map<uint32_t, SsrcParameter> ssrcs;
    SimulcastParameter simulcast;
  };
}
}