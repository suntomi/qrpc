#pragma once

#include "base/defs.h"
#include "base/media.h"

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
    Producer(RTC::Shared* s, const std::string& id, Listener* l, json& d) :
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
    std::vector<std::shared_ptr<Producer>> &producers() { return producers_; }
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
    std::vector<std::shared_ptr<Producer>> producers_;
		// Table of SSRC / Producer pairs.
		std::unordered_map<uint32_t, Producer*> ssrcTable;
		//  Table of MID / Producer pairs.
		std::unordered_map<std::string, Producer*> midTable;
		//  Table of RID / Producer pairs.
		std::unordered_map<std::string, Producer*> ridTable;
  }; 
}
}