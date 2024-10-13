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
  class Producer : public RTC::Producer {
    friend class ProducerFactory;
  public:
    Producer(
      RTC::Shared* s, const std::string& id, Listener* l, 
      const Parameters &p, std::shared_ptr<Media> m
    ) : RTC::Producer(s, id, l, p.MakeProduceRequest(id)), params_(p), media_(m) {}
    ~Producer() override {}
    std::vector<::flatbuffers::Offset<FBS::RtpParameters::RtpEncodingParameters>>
    PackConsumableEncodings(::flatbuffers::FlatBufferBuilder &fbb) const;
    bool consume_params(const RTC::RtpParameters &consumed_producer_params, RTC::RtpParameters &p) const;
    const RTC::RtpParameters &params() const { return params_; }
  protected:
    Parameters params_;
    std::shared_ptr<Media> media_;
  };
  class ProducerFactory {
  public:
    ProducerFactory(Handler &h) : handler_(h) {}
    virtual ~ProducerFactory() {}
    std::map<std::string, std::shared_ptr<Producer>> &producers() { return producers_; }
    static std::string GenerateId(const std::string &id, const std::string &label, Parameters::MediaKind kind);
  public:
    std::shared_ptr<Producer> Create(const std::string &id, const Parameters &p);
    Producer *Get(const RTC::RtpPacket &p);
    inline Producer *Get(uint32_t ssrc) {
      auto it = ssrcTable.find(ssrc);
      if (it != ssrcTable.end()) {
        return it->second;
      }
      return nullptr;
    }
  protected:
    bool Add(std::shared_ptr<Producer> &p);
    void Remove(std::shared_ptr<Producer> &p);
  protected:
    Handler &handler_;
    std::map<std::string, std::shared_ptr<Producer>> producers_;
		// Table of SSRC / Producer pairs.
		std::unordered_map<uint32_t, Producer*> ssrcTable;
		//  Table of MID / Producer pairs.
		std::unordered_map<std::string, Producer*> midTable;
		//  Table of RID / Producer pairs.
		std::unordered_map<std::string, Producer*> ridTable;
  }; 
}
}