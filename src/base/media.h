#pragma once

#include "RTC/RtpPacket.hpp"

namespace base {
  class Media {
  public:
    typedef std::string Id;
  public:
    Media(const std::string &l) : label_(l) {}
    const std::string &label() const { return label_; }
    virtual void OnData() = 0;
  protected:
    std::string label_;
  };
  class AdhocMedia : public Media {
  public:
    typedef std::function<void (RTC::RtpPacket &)> Handler; 
  public:
    AdhocMedia(const std::string &l, Handler &&h) : Media(l), handler_(h) {}
  protected:
    Handler handler_;
  };
}