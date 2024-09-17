#pragma once

#include <stdint.h>
#include <string>

#include "base/webrtc.h"

#include "sdptransform.hpp"
#include <nlohmann/json.hpp>
using json = nlohmann::json;

namespace base {
namespace rtp {
  class Parameters;
}
namespace webrtc {
  // for SDP example, see example/client/main.cpp test_sdp()
  class SDP : public json {
  public:
    SDP(const std::string &sdp) : json(sdptransform::parse(sdp)) {}
    ~SDP() {}
  public:
    // connection is not const reference because it might be configured with SDP
    bool Answer(ConnectionFactory::Connection &c, std::string &answer) const;
    static int Offer(const ConnectionFactory::Connection &c, 
      const std::string &ufrag, const std::string &pwd, std::string &offer);
  public:
    std::vector<Candidate> Candidates() const;
    bool FindMediaSection(const std::string &type, json &j) const;
  protected:
    std::string CandidatesSDP(const std::string &proto, ConnectionFactory::Connection &c) const;
    bool GetRemoteFingerPrint(const json &section, std::string &answer, DtlsTransport::Fingerprint &ret) const;
    bool AnswerMediaSection(
      const json &section, const std::string &proto, ConnectionFactory::Connection &c,
      std::string &answer, rtp::Parameters &param) const;
    uint32_t AssignPriority(uint32_t component_id) const;
  };
} // namespace webrtc
} // namespace base