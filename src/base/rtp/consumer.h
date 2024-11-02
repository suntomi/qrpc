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
    static std::string GenerateId(const std::string &id, const std::string &label, Parameters::MediaKind kind);
  public:
    std::shared_ptr<Consumer> Create(
      const Handler &peer, const Producer &local_producer, const std::string &label, Parameters::MediaKind kind,
      bool paused, bool pipe, std::vector<uint32_t> &generated_ssrcs);
  protected:
    Handler &handler_;
    std::map<std::string, std::shared_ptr<Consumer>> consumers_;
  }; 
}
}