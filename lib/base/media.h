#pragma once

#include <string>

namespace base {
  class Media {
  public:
    typedef std::string Id; // label of the media
    typedef std::string Rid; // Rid of RTP stream
    typedef std::string Mid; // Rid of RTP stream
    typedef std::string TrackId; // track id of WebRTC js API
    typedef std::string ScalabilityMode; // scalability mode of RTP stream (SVC)
    typedef uint64_t Ssrc; // Ssrc of RTP stream
  public:
    Media(const std::string &l) : label_(l) {}
    const std::string &label() const { return label_; }
  protected:
    std::string label_;
  };
}