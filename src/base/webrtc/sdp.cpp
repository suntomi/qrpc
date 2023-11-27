#include "base/webrtc/sdp.h"
#include "base/logger.h"
#include "base/string.h"

namespace base {
  bool SDP::Answer(const WebRTCServer::Connection &c, std::string &answer) const {
    for (auto it = find("media"); it != end(); ++it) {
      auto proto = it->find("protocol");
      if (proto == it->end()) {
        logger::warn({{"ev","media does not have protocol"}, {"media", *it}});
        ASSERT(false);
        continue;
      }
      if ((*proto) == "UDP/DTLS/SCTP") {
        // answer udp port
        answer = AnswerAs("UDP", c);
        return true;
      } else if ((*proto) == "TCP/DTLS/SCTP") {
        // answer tcp port
        answer = AnswerAs("TCP", c);
        return true;
      } else {
        logger::debug({{"ev","non SCTP media protocol"}, {"media", *proto}});
        continue;
      }
    }
    answer = "no data channel media found";
    return false;
  }
  std::string SDP::AnswerAs(const std::string &proto, const WebRTCServer::Connection &c) const {
    auto now = qrpc_time_now();
    return str::Format(R"sdp(v=0
o=- %llu %llu IN IP4 127.0.0.1
s=-
t=0 0
a=group:BUNDLE 0
a=msid-semantic: WMS
m=application 9 %s/DTLS/SCTP webrtc-datachannel
c=IN IP4 0.0.0.0
b=AS:30
a=ice-ufrag:%s
a=ice-pwd:%s
a=ice-options:trickle
a=fingerprint:sha-256 %s
a=setup:active
a=mid:0
a=sctp-port:%u
a=max-message-size:%u
)sdp",
    now, now,
    proto,
    c.ice_server()->GetUsernameFragment(),
    c.ice_server()->GetPassword(),
    c.server().fingerprint(),
    proto == "UDP" ? c.server().udp_port() : c.server().tcp_port(),
    c.server().config().sctp_send_buffer_size
  );
  }
  bool SDP::Test() {
    auto text = R"sdp(v=0
o=- 2079181553264408257 2 IN IP4 127.0.0.1
s=-
t=0 0
a=group:BUNDLE 0
a=extmap-allow-mixed
a=msid-semantic: WMS
m=application 9 UDP/DTLS/SCTP webrtc-datachannel
c=IN IP4 0.0.0.0
b=AS:30
a=ice-ufrag:5xf5
a=ice-pwd:RaGh41Km50SazV5xD4cU0KNL
a=ice-options:trickle
a=fingerprint:sha-256 82:1E:F4:77:79:BF:31:AC:90:F6:0C:91:FB:FE:C5:0A:39:47:63:2E:18:A4:0F:36:7C:53:7A:D2:B8:91:42:A3
a=setup:active
a=mid:0
a=sctp-port:5000
a=max-message-size:262144
)sdp";
    SDP sdp(text);
    logger::info(sdp);
    return false;
  }
}