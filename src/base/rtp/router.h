#pragma once

#include "base/defs.h"
#include "base/rtp/shared.h"

#include "RTC/Router.hpp"

#include <thread>

namespace base {
  class Stream;
namespace rtp {
  class Router : public RTC::Router {
  public:
    static std::string to_string(std::thread::id id) {
      std::stringstream ss;
      ss << id;
      return ss.str();
    }
    Router(RTC::Shared *shared, Listener *l) : RTC::Router(shared, to_string(std::this_thread::get_id()), l) {}
    void Publish(const Stream *publisher) {
      publish_map_[publisher];
    }
    void Unpublish(const Stream *publisher) {
      publish_map_.erase(publisher);
    }
    const std::vector<Stream *> &SubscribersFor(const Stream *publisher) const {
      static std::vector<Stream *> empty_;
      auto pit = publish_map_.find(publisher);
      return pit == publish_map_.end() ? empty_ : pit->second;
    }
    bool Subscribe(const Stream *publisher, Stream *subscriber) {
      auto sit = subscribe_map_.find(subscriber);
      if (sit != subscribe_map_.end()) {
        ASSERT(false);
        return false;
      }
      subscribe_map_[subscriber] = publisher;
      publish_map_[publisher].push_back(subscriber);
      return true;
    }
    void Unsubscribe(const Stream *subscriber) {
      auto sit = subscribe_map_.find(subscriber);
      if (sit != subscribe_map_.end()) {
        auto publisher = sit->second;
        subscribe_map_.erase(sit);
        auto pit = publish_map_.find(publisher);
        if (pit == publish_map_.end()) {
          return;
        }
        auto &subscribers = pit->second;
        subscribers.erase(
          std::remove(subscribers.begin(), subscribers.end(), subscriber),
          subscribers.end()
        );
      } else {
        ASSERT(false);
      }
    }
  protected:
    std::map<const Stream*, std::vector<Stream*>> publish_map_;
    std::map<const Stream*, const Stream*> subscribe_map_;
  };
}
}