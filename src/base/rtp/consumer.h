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
  class MediaStreamConfig;
  typedef RTC::Consumer Consumer;
  class ConsumerFactory {
  public:
    ConsumerFactory(Handler &h) : handler_(h) {}
    virtual ~ConsumerFactory() {}
    static std::string GenerateId(const std::string &id, const std::string &path);
  public:
    Consumer *Create(
      const Handler &peer, const MediaStreamConfig &config, bool pipe
    );
    flatbuffers::Offset<FBS::Consumer::DumpResponse>
    FillBuffer(Consumer *c, flatbuffers::FlatBufferBuilder& builder);
    static Handler &HandlerFrom(Consumer *c);
  protected:
    Handler &handler_;
  }; 
}
}