#include "base/rtp/capability.h"

namespace base {
namespace rtp {
  RTC::RtpCodecParameters *Capability::CodecByPayloadType(uint64_t pt) {
    for (auto &c : codecs) {
      if (c.payloadType == pt) {
        return &c;
      }
    }
    return nullptr;
  }
}
}