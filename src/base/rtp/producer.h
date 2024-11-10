#pragma once

#include "base/defs.h"
#include "base/media.h"
#include "base/rtp/parameters.h"

#include "RTC/Producer.hpp"
#include "RTC/RtpPacket.hpp"
#include "RTC/Shared.hpp"

namespace base {
namespace rtp {
  class Handler;
  class Parameters;
  class Producer : public RTC::Producer {
    friend class ProducerFactory;
  public:
    Producer(
      RTC::Shared* s, const std::string& id, Listener* l, 
      const Parameters &original_params,
      const FBS::Transport::ProduceRequest *p, std::shared_ptr<Media> m
    ) : RTC::Producer(s, id, l, p), params_(original_params), media_(m) {}
    ~Producer() override {}
    bool consumer_params(const RTC::RtpParameters &consumed_producer_params, RTC::RtpParameters &p) const;
    const Parameters &params() const { return params_; }
  protected:
    Parameters params_;
    std::shared_ptr<Media> media_;
  };
  class ProducerFactory {
  public:
    ProducerFactory(Handler &h) : handler_(h) {}
    virtual ~ProducerFactory() {}
    static std::string GenerateId(const std::string &id, const std::string &label, Parameters::MediaKind kind);
  public:
    Producer *Create(const std::string &id, const Parameters &p);
  protected:
    Handler &handler_;
  }; 
}
}