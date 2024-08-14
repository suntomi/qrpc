#pragma once

#include "base/webrtc/sdp.h"

#include "RTC/RtpDictionaries.hpp"

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
    struct SimulcastParameters {
      std::string send_rids, recv_rids;
    };
  public:
    Parameters() : RTC::RtpParameters() {}
    int Parse(Handler &h, const json &section, std::string &answer);
    std::string Answer() const;
    RTC::RtpCodecParameters *CodecByPayloadType(uint64_t pt);
  public:
    json &&ToJson() const;
    json &&CodecToJson() const;
    json &&CodecsToJson() const;
    json &&ExtensionsToJson() const;
    json &&EncodingsToJson() const;
    json &&RtcpToJson() const;
  public:
    std::string media_type; // video/audio/application
    NetworkParameters network;
    std::map<uint32_t, SsrcParameter> ssrcs;
    SimulcastParameters simulcast;
  };
}
}