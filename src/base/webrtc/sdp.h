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
    bool AnswerMediaSection(
      const json &section, const std::string &proto, ConnectionFactory::Connection &c,
      rtp::Parameters &params, std::string &errmsg) const;
    std::vector<std::string> GetMids() const;
  public:
    struct AnswerParams {
      const rtp::Parameters *params;
      std::string cname;
      AnswerParams() : params(nullptr), cname("") {}
      AnswerParams(const rtp::Parameters &p) : params(&p), cname("") {}
      AnswerParams(const rtp::Parameters &p, const std::string &cname) : params(&p), cname(cname) {}
      const AnswerParams & operator = (const AnswerParams &p) {
        params = p.params;
        cname = p.cname;
        return *this;
      }
    };
    static std::string GenerateAnswer(
      ConnectionFactory::Connection &c, const std::string &proto,
      const std::vector<rtp::Parameters> &params
    ) {
      std::vector<AnswerParams> anwser_params;
      for (const auto &p : params) { anwser_params.emplace_back(p); }
      return GenerateAnswer(c, proto, anwser_params);
    }
    static std::string GenerateAnswer(
      ConnectionFactory::Connection &c, const std::string &proto,
      const std::vector<AnswerParams> anwser_params
    );
    static inline std::string GenerateSectionAnswer(ConnectionFactory::Connection &c, 
      const std::string &proto, const AnswerParams &p);
    static std::string CandidatesSDP(const std::string &proto, ConnectionFactory::Connection &c);
    static uint32_t AssignPriority(uint32_t component_id);
    static bool CreateSectionAnswer(
      std::vector<AnswerParams> &section_answers, const std::vector<std::string> &mids,
      const std::vector<rtp::Parameters> &params, std::string &error);
  public:
  };
} // namespace webrtc
} // namespace base