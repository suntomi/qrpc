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
    bool Answer(const std::map<std::string, std::string> mid_label_map,
      ConnectionFactory::Connection &c, std::string &answer) const;
    static int Offer(const ConnectionFactory::Connection &c,
      const std::string &ufrag, const std::string &pwd, std::string &offer);
  public:
    std::vector<Candidate> Candidates() const;
    bool FindMediaSection(const std::string &type, json &j) const;
  protected:
    bool GetRemoteFingerPrint(const json &section, std::string &answer, RTC::DtlsTransport::Fingerprint &ret) const;
    bool AnswerMediaSection(
      const json &section, const std::string &proto, const std::map<std::string, std::string> mid_label_map,
      ConnectionFactory::Connection &c, rtp::Handler::MediaStreamConfig &params, std::string &errmsg) const;
  public:
    static bool GenerateAnswer(
      ConnectionFactory::Connection &c, const std::string &proto,
      const rtp::Handler::MediaStreamConfigs &configs, std::string &answer
    );
    static inline bool GenerateSectionAnswer(ConnectionFactory::Connection &c, 
      const std::string &proto, const rtp::Handler::MediaStreamConfig &p, std::string &answer);
    static std::string CandidatesSDP(const std::string &proto, ConnectionFactory::Connection &c);
    static uint32_t AssignPriority(uint32_t component_id);
  public:
  };
} // namespace webrtc
} // namespace base