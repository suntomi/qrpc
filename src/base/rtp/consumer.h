#pragma once

#include "base/defs.h"
#include "base/media.h"

#include "RTC/Consumer.hpp"
#include "RTC/RtpPacket.hpp"
#include "RTC/Shared.hpp"

namespace base {
namespace rtp {
  class Handler;
  class Parameters;
  class Consumer : public RTC::Consumer {
    friend class ProducerFactory;
  public:
    Consumer(RTC::Shared* s, 
      const std::string& id, const std::string& producer_id,
      Listener* l, json& d, RTC::RtpParameters::Type type
    ) : RTC::Consumer(s, id, producer_id, l, d, type) {}
    ~Consumer() override {}
  protected:
    void SetMedia(std::shared_ptr<Media> m) { media_ = m; }
    std::shared_ptr<Media> media_;
  };
  class ConsumerFactory {
  public:
    ConsumerFactory(Handler &h) : handler_(h) {}
    virtual ~ConsumerFactory() {}
    std::vector<std::shared_ptr<Consumer>> &consumers() { return consumers_; }
  public:
    std::shared_ptr<Consumer> Create(const std::string &id, const Parameters &p);
    Consumer *Get(const RTC::RtpPacket &p);
  protected:
    bool Add(std::shared_ptr<Consumer> &p);
    void Remove(std::shared_ptr<Consumer> &p);
  protected:
    Handler &handler_;
    std::vector<std::shared_ptr<Consumer>> consumers_;
  }; 
}
}