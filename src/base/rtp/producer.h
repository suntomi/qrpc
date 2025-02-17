#pragma once

#include "base/defs.h"
#include "base/media.h"
#include "base/rtp/capability.h"
#include "base/rtp/parameters.h"

#include "RTC/Producer.hpp"
#include "RTC/RtpPacket.hpp"
#include "RTC/Shared.hpp"

namespace base {
namespace rtp {
  class Handler;
  class Parameters;
  class MediaStreamConfig;
  struct ProducerStatus {
    bool paused{ false };
    json ToJson() const;
  };
  class Producer : public RTC::Producer {
    friend class ProducerFactory;
  public:
    Producer(
      RTC::Shared* s, const std::string& id, Listener* l, 
      const Parameters &original_params,
      const FBS::Transport::ProduceRequest *p, std::shared_ptr<Media> m
    ) : RTC::Producer(s, id, l, p), params_(original_params), media_(m) {}
    ~Producer() override {}
    const Parameters &params() const { return params_; }
    const std::string &media_path() const { return media_->label(); }
    ProducerStatus status() const;
  public:
    static bool consumer_params(
      const RTC::RtpParameters &consumed_producer_params, const Capability &consumer_capability, Parameters &p
    );
  protected:
    Parameters params_;
    std::shared_ptr<Media> media_;
  };
  class ProducerFactory {
  public:
    ProducerFactory(Handler &h) : handler_(h) {}
    virtual ~ProducerFactory() {}
    static std::string GenerateId(const std::string &rtp_id, const std::string &path);
  public:
    Producer *Create(const MediaStreamConfig &p);
  protected:
    Handler &handler_;
  }; 
}
}