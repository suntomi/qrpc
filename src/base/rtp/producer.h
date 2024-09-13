#pragma once

#include "base/defs.h"
#include "base/media.h"

#include "base/rtp/parameters.h"
#include "base/rtp/shared.h"

#include "RTC/Producer.hpp"
#include "RTC/RtpPacket.hpp"

namespace base {
class Media;
namespace rtp {
  class Handler;
  class Producer : public RTC::Producer {
    friend class ProducerFactory;
  public:
    Producer(RTC::Shared* s, const std::string& id, RTC::Producer::Listener* l, json& d) :
      RTC::Producer(s, id, l, d) {}
    ~Producer() override {}
  protected:
    void SetMedia(std::shared_ptr<Media> m) { media_ = m; }
    std::shared_ptr<Media> media_;
  };
  class ProducerFactory {
  public:
    ProducerFactory(Handler &h) : handler_(h) {}
    virtual ~ProducerFactory() {}
    std::shared_ptr<RTC::Producer> Create(const std::string &id, const Parameters &p);
    RTC::Producer *Get(const RTC::RtpPacket &p);
    inline RTC::Producer *Get(uint32_t ssrc) {
      auto it = ssrcTable.find(ssrc);
      if (it != ssrcTable.end()) {
        return it->second;
      }
      return nullptr;
    }
  protected:
    bool Add(std::shared_ptr<RTC::Producer> p);
    void Remove(std::shared_ptr<RTC::Producer> p);
  protected:
    Handler &handler_;
    std::vector<std::shared_ptr<RTC::Producer>> producers_;
		// Table of SSRC / Producer pairs.
		std::unordered_map<uint32_t, RTC::Producer*> ssrcTable;
		//  Table of MID / Producer pairs.
		std::unordered_map<std::string, RTC::Producer*> midTable;
		//  Table of RID / Producer pairs.
		std::unordered_map<std::string, RTC::Producer*> ridTable;
  }; 
}
}