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
    std::map<std::string, std::shared_ptr<Consumer>> &consumers() { return consumers_; }
  public:
    std::shared_ptr<Consumer> Create(
      const Producer &consumed_producer, const std::string &label, Parameters::MediaKind kind,
      RTC::RtpParameters::Type type, const RTC::RtpParameters &p, bool paused);
  protected:
    Handler &handler_;
    std::map<std::string, std::shared_ptr<Consumer>> consumers_;
  }; 
}
}