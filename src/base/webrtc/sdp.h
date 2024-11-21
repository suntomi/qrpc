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
    bool GetRemoteFingerPrint(const json &section, std::string &answer, RTC::DtlsTransport::Fingerprint &ret) const;
    rtp::Parameters *AnswerMediaSection(
      const json &section, const std::string &proto, ConnectionFactory::Connection &c,
      std::map<std::string, rtp::Parameters> &section_answer_map, std::string &errmsg) const;
  public:
    static std::string GenerateAnswer(
      ConnectionFactory::Connection &c, const std::string &proto,
      const std::map<std::string, rtp::Parameters> &params_map, bool for_consumer
    ) {
      std::map<std::string, const rtp::Parameters*> params_map_ref;
      for (const auto &kv : params_map) { params_map_ref[kv.first] = &kv.second; }
      return GenerateAnswer(c, proto, params_map_ref, for_consumer);
    }
    static std::string GenerateAnswer(
      ConnectionFactory::Connection &c, const std::string &proto,
      const std::map<std::string, const rtp::Parameters*> &params_map, bool for_consumer
    );
    static inline std::string GenerateSectionAnswer(ConnectionFactory::Connection &c,
      const std::string &proto, const rtp::Parameters &p, bool for_consumer);
    static std::string CandidatesSDP(const std::string &proto, ConnectionFactory::Connection &c);
    static uint32_t AssignPriority(uint32_t component_id);
  public:
  };
} // namespace webrtc
} // namespace base