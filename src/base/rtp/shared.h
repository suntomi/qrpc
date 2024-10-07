#pragma once

#include "RTC/Shared.hpp"

namespace base {
namespace rtp {
  class Shared {
  public:
    Shared() : cs_(-1, -1), shared_(
      new ChannelMessageRegistrator(),
      new Channel::ChannelNotifier(&cs_)
    ) {}
    ~Shared() {}
    RTC::Shared &get() { return shared_; }
  protected:
    Channel::ChannelSocket cs_;
    RTC::Shared shared_;
  };
}
}