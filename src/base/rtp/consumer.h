#pragma once

#include "base/defs.h"
#include "base/media.h"
#include "base/rtp/parameters.h"

#include "RTC/Consumer.hpp"
#include "RTC/RtpPacket.hpp"
#include "RTC/Shared.hpp"

namespace base {
namespace rtp {
  class Handler;
  class Producer;
  typedef RTC::Consumer Consumer;
  class ConsumerFactory {
  public:
    ConsumerFactory(Handler &h) : handler_(h) {}
    virtual ~ConsumerFactory() {}
    inline std::string GenerateMid() {
      auto mid = mid_seed_++;
      if (mid_seed_ > 1000000000) { ASSERT(false); mid_seed_ = 0; } 
      return std::to_string(mid);
    }
    static std::string GenerateId(
      const std::string &id, const std::string &peer_id, const std::string &label, Parameters::MediaKind kind);
  public:
    Consumer *Create(
      const Handler &peer, const std::string &label, Parameters::MediaKind kind,
      const RTC::RtpParameters &consumer_params, bool paused, bool pipe
    );
    flatbuffers::Offset<FBS::Consumer::DumpResponse>
    FillBuffer(Consumer *c, flatbuffers::FlatBufferBuilder& builder);
  protected:
    uint32_t mid_seed_{0};
    Handler &handler_;
  }; 
}
}