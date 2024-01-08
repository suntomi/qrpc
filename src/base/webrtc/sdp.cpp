#include "base/webrtc/sdp.h"
#include "base/logger.h"
#include "base/string.h"

namespace base {
namespace webrtc {
  int SDP::Offer(const ConnectionFactory::Connection &c,
    const std::string &uflag, const std::string &pwd, std::string &offer) {
    auto now = qrpc_time_now();
    offer = str::Format(R"sdp(v=0
o=- %llu %llu IN IP4 0.0.0.0
s=-
t=0 0
a=group:BUNDLE 0
a=extmap-allow-mixed
a=msid-semantic: WMS
m=application 9 %s/DTLS/SCTP webrtc-datachannel
c=IN IP4 0.0.0.0
b=AS:30
a=ice-ufrag:%s
a=ice-pwd:%s
a=ice-options:trickle
a=fingerprint:%s %s
a=setup:active
a=mid:0
a=sctp-port:5000
a=max-message-size:%u
)sdp",
      now, now,
      c.factory().primary_proto(),
      uflag, pwd,
      c.factory().fingerprint_algorithm().c_str(), c.factory().fingerprint().c_str(),
      c.factory().config().send_buffer_size
    );
    return QRPC_OK;
  }
  std::vector<Candidate> SDP::Candidates() const {
    std::vector<Candidate> v;
    auto mit = find("media");
    if (mit == end()) {
      ASSERT(false);
      return v;
    }
    for (auto it = mit->begin(); it != mit->end(); ++it) {
      auto protoit = it->find("protocol");
      bool dgram = false;
      if ((*protoit) == "UDP/DTLS/SCTP") {
        dgram = true;
      } else if ((*protoit) == "TCP/DTLS/SCTP") {
      } else {
        QRPC_LOGJ(warn, {{"ev","non SCTP media protocol"}, {"proto", *protoit}});
        continue;
      }
      auto hostit = it->find("host");
      if (hostit == it->end()) {
        QRPC_LOGJ(warn, {{"ev","non host value"},{"sdp",json::dump(*this)}});
        ASSERT(false);
        continue;
      }
      auto portit = it->find("port");
      if (portit == it->end()) {
        QRPC_LOGJ(warn, {{"ev","non port valuests"},{"sdp",json::dump(*this)}});
        ASSERT(false);
        continue;
      }
      auto uflagit = it->find("uflag");
      if (uflagit == it->end()) {
        QRPC_LOGJ(warn, {{"ev","non uflag value"},{"sdp",json::dump(*this)}});
        ASSERT(false);
        continue;
      }
      auto pwdit = it->find("pwd");
      if (pwdit == it->end()) {
        QRPC_LOGJ(warn, {{"ev","non pwd valuests"},{"sdp",json::dump(*this)}});
        ASSERT(false);
        continue;
      }
      v.push_back(Candidate(dgram, *hostit, *portit, *uflagit, *pwdit));
    }
    return v;
  }
  bool SDP::Answer(ConnectionFactory::Connection &c, std::string &answer) const {
    auto mit = find("media");
    if (mit == end()) {
      answer = "no media found";
      ASSERT(false);
      return false;
    }
    // firefox contains fingerprint in root section, not in media
    auto root_fp = find("fingerprint");
    for (auto it = mit->begin(); it != mit->end(); ++it) {
      auto proto = it->find("protocol");
      if (proto == it->end()) {
        logger::warn({{"ev","media does not have protocol"}, {"media", *it}});
        ASSERT(false);
        continue;
      }
      if ((*proto) == "UDP/DTLS/SCTP") {
        // answer udp port
        answer = AnswerAs("UDP", c);
      } else if ((*proto) == "TCP/DTLS/SCTP") {
        // answer tcp port
        answer = AnswerAs("TCP", c);
      } else {
        logger::debug({{"ev","non SCTP media protocol"}, {"proto", *proto}});
        continue;
      }
      // protocol found. set remote fingerprint
      // TODO: move this to dedicated function but type declaration of it is not easy
      auto fp = root_fp;
      if (fp == end()) {
        fp = it->find("fingerprint");
        if (fp == it->end()) {
          logger::error({{"ev","malform sdp"},{"reason","no fingerprint"}, {"as_json", dump()}});
          // malicious?
          answer = "no fingerprint found";
          ASSERT(false);
          return false;
        }
      }
      auto type = fp->find("type");
      auto hash = fp->find("hash");
      if (type == fp->end() || hash == fp->end()) {
        logger::error({{"ev","malform sdp"},{"reason","no fingerprint type or hash"}, {"as_json", dump()}});
        // malicious?
        answer = "no fingerprint type or hash found";
        ASSERT(false);
        return false;
      }
      auto algo = DtlsTransport::GetFingerprintAlgorithm(*type);
      if (algo == DtlsTransport::FingerprintAlgorithm::NONE) {
        answer = "unknown fingerprint algorithm:" + type->get<std::string>();
        ASSERT(false);
        return false;
      }
      c.dtls_transport().SetRemoteFingerprint({.algorithm = algo, .value = *hash});
      return true;
    }
    answer = "no data channel media found";
    return false;
  }
  uint32_t SDP::AssignPriority(uint32_t component_id) const {
    // borrow from
    // https://github.com/IIlllII/bitbreeds-webrtc/blob/master/webrtc-signaling/src/main/java/com/bitbreeds/webrtc/signaling/SDPUtil.java#L191
    return 2113929216 + 16776960 + (256 - component_id);
  }

  std::string SDP::AnswerAs(const std::string &proto, const ConnectionFactory::Connection &c) const {
    auto now = qrpc_time_now();
    auto port = proto == "UDP" ? c.factory().udp_port() : c.factory().tcp_port();
    auto candidates = std::string("");
    size_t idx = 0;
    for (auto &a : c.factory().config().ifaddrs) {
      candidates += str::Format(
        "%sa=candidate:0 %u %s %u %s %u typ host",
        idx == 0 ? "" : "\n",
        idx + 1, proto.c_str(), AssignPriority(idx), a.c_str(), port
      );
      idx++;
    }
    return str::Format(R"sdp(v=0
o=- %llu %llu IN IP4 0.0.0.0
s=-
t=0 0
a=group:BUNDLE 0
a=msid-semantic: WMS
m=application 9 %s/DTLS/SCTP webrtc-datachannel
c=IN IP4 0.0.0.0
b=AS:30
a=sendrecv
%s
a=end-of-candidates
a=ice-lite
a=ice-ufrag:%s
a=ice-pwd:%s
a=fingerprint:%s %s
a=setup:active
a=mid:0
a=sctp-port:5000
a=max-message-size:%u
)sdp",
      now, now,
      proto.c_str(),
      candidates.c_str(),
      c.ice_server().GetUsernameFragment().c_str(),
      c.ice_server().GetPassword().c_str(),
      c.factory().fingerprint_algorithm().c_str(), c.factory().fingerprint().c_str(),
      c.factory().config().send_buffer_size
    );
  }
} // namespace webrtc
} // namespace base