#pragma once

#include "RTC/RtpDictionaries.hpp"

#include <vector>

namespace base {
namespace rtp {
  class Capability {
  public:
    Capability() {}
    RTC::RtpCodecParameters *CodecByPayloadType(uint64_t pt);
  public:
    std::vector<RTC::RtpCodecParameters> codecs;
    std::vector<RTC::RtpHeaderExtensionParameters> headerExtensions;
  };
}
}