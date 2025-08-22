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
  struct MediaStreamConfig;
  struct RemoteAnswer;
  struct ProducerStatus {
    bool paused{ false };
    json ToJson() const;
  };
  class Producer : public RTC::Producer {
    friend class ProducerFactory;
  public:
    Producer(
      RTC::Shared* s, const std::string& id, Listener* l, 
      Handler &handler, const FBS::Transport::ProduceRequest *p, std::shared_ptr<Media> m
    ) : RTC::Producer(s, id, l, p), handler_(handler), media_(m) {}
    ~Producer() override {}
    const Parameters *params() const;
    inline const std::string media_path() const { return media_->label() + "/" + Parameters::FromMediaKind(GetKind()); }
    ProducerStatus status() const;
  public:
    static bool consumer_params(
      const RTC::RtpParameters &consumed_producer_params, const Capability &consumer_capability, Parameters &p
    );
    bool ApplyAnswer(const RemoteAnswer &answer, std::string &error);
  protected:
    bool ReplaceEncodings(std::vector<RTC::RtpEncodingParameters> &encodings, uint32_t old_ssrc, uint32_t new_ssrc);
  protected:
    Handler &handler_;
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