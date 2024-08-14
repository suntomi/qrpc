#pragma once

#include "base/rtp/handler.h"

namespace base {
  class Media {
  public:
    typedef std::string Id; // label of the media
    typedef std::string Rid; // Rid of RTP stream
    typedef std::string TrackId; // track id of WebRTC js API
    typedef uint64_t Ssrc; // Ssrc of RTP stream
  public:
    Media(const std::string &l) : label_(l) {}
    const std::string &label() const { return label_; }
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