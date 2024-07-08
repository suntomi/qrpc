#pragma once

#include <stdint.h>
#include <string>

#include "base/webrtc.h"

#include "sdptransform.hpp"
#include <nlohmann/json.hpp>
using json = nlohmann::json;

// chrome
// client -> server offer
// "v=0
// o=- 2079181553264408257 2 IN IP4 127.0.0.1
// s=-
// t=0 0
// a=group:BUNDLE 0
// a=extmap-allow-mixed
// a=msid-semantic: WMS
// m=application 9 UDP/DTLS/SCTP webrtc-datachannel
// c=IN IP4 0.0.0.0
// b=AS:30
// a=ice-ufrag:5xf5
// a=ice-pwd:RaGh41Km50SazV5xD4cU0KNL
// a=ice-options:trickle
// a=fingerprint:sha-256 82:1E:F4:77:79:BF:31:AC:90:F6:0C:91:FB:FE:C5:0A:39:47:63:2E:18:A4:0F:36:7C:53:7A:D2:B8:91:42:A3
// a=setup:active
// a=mid:0
// a=sctp-port:5000
// a=max-message-size:262144
// "

// server -> client answer
// v=0\r\n' +
//     'o=- 1678775933238558874 2 IN IP4 127.0.0.1\r\n' +
//     's=-\r\n' +
//     't=0 0\r\n' +
//     'a=group:BUNDLE 0\r\n' +
//     'a=extmap-allow-mixed\r\n' +
//     'a=msid-semantic: WMS\r\n' +
//     'm=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n' +
//     'c=IN IP4 0.0.0.0\r\n' +
//     'a=ice-ufrag:LIMJ\r\n' +
//     'a=ice-pwd:D2ESJ9b/QuEsKt3vABkqwcaB\r\n' +
//     'a=ice-options:trickle\r\n' +
//     'a=fingerprint:sha-256 5B:C2:B1:21:F0:D6:60:07:52:C2:D0:08:2B:5D:1D:A9:91:7F:EA:78:38:52:E0:F9:73:C6:D8:35:09:BD:CC:C2\r\n' +
//     'a=setup:actpass\r\n' +
//     'a=mid:0\r\n' +
//     'a=sctp-port:5000\r\n' +
//     'a=max-message-size:262144\r\n'

// firefox
// client -> server offer
// 'v=0\r\n' +
//     'o=mozilla...THIS_IS_SDPARTA-99.0 5646051690976802063 0 IN IP4 0.0.0.0\r\n' +
//     's=-\r\n' +
//     't=0 0\r\n' +
//     'a=sendrecv\r\n' +
//     'a=fingerprint:sha-256 E9:A7:6F:47:C0:8D:D6:8D:03:7C:A7:D6:33:12:4C:1D:90:65:DC:2E:1D:BF:CF:2F:2D:72:2C:8C:3F:95:4A:CE\r\n' +
//     'a=group:BUNDLE 0\r\n' +
//     'a=ice-options:trickle\r\n' +
//     'a=msid-semantic:WMS *\r\n' +
//     'm=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n' +
//     'c=IN IP4 0.0.0.0\r\n' +
//     'a=sendrecv\r\n' +
//     'a=ice-pwd:df99ba0c36c2fc7766b925c17f6c8287\r\n' +
//     'a=ice-ufrag:a635b2f0\r\n' +
//     'a=mid:0\r\n' +
//     'a=setup:actpass\r\n' +
//     'a=sctp-port:5000\r\n' +
//     'a=max-message-size:1073741823\r\n'

// server -> client answer
// v=0
// o=- 2021133599155150727 2 IN IP4 127.0.0.1
// s=-
// t=0 0
// a=group:BUNDLE 0
// a=msid-semantic: WMS
// m=application 9 UDP/DTLS/SCTP webrtc-datachannel
// c=IN IP4 0.0.0.0
// b=AS:30
// a=ice-ufrag:6XDh
// a=ice-pwd:p9rktMWhyKXPmz0Is9hZ2pGr
// a=ice-options:trickle
// a=fingerprint:sha-256 C8:01:63:AF:7F:EA:0F:69:08:C1:26:B0:B8:7A:2C:DF:88:DF:AB:46:6B:F4:04:6A:7A:D4:01:3F:F7:29:38:86
// a=setup:active
// a=mid:0
// a=sctp-port:5000
// a=max-message-size:262144

namespace base {
namespace webrtc {
  // v=0
  // o=- $timestamp $timestamp IN IP4 0.0.0.0
  // s=-
  // m=application $udp_port UDP/DTLS/SCTP 5000
  // a=sctpmap:5000 webrtc-datachannel 1024
  // a=ice-lite
  // a=ice-ufrag:289b31b754eaa438
  // a=ice-pwd:0b66f472495ef0ccac7bda653ab6be49ea13114472a5d10a
  // a=ice-options:trickle
  // a=fingerprint:sha-256 AA:3D:E4:BE:A4:95:CF:A2:32:62:6E:A5:C9:67:AF:C6:6D:3D:57:3D:35:61:7D:A9:55:06:EA:B5:54:C5:96:81
  // a=setup:active
  // a=max-message-size:262144
  // a=sctp-port:$port
  // a=candidate:1 1 UDP 2130706431 a.b.c.d $port typ host
  // ...
  // a=candidate:2 1 TCP 1694498815 x.y.z.w $port typ host
  // ...
  class SDP : public json {
  public:
    SDP(const std::string &sdp) : json(sdptransform::parse(sdp)) {}
    ~SDP() {}
  public:
    // connection is not const reference because it might be configured with SDP
    bool Answer(ConnectionFactory::Connection &c, const SDP &client_sdp, std::string &answer) const;
    static int Offer(const ConnectionFactory::Connection &c, 
      const std::string &ufrag, const std::string &pwd, std::string &offer);
  public:
    std::vector<Candidate> Candidates() const;
    bool FindMediaSection(const std::string &type, json &j) const;
  protected:
    bool GetRemoteFingerPrint(const json::const_iterator &it, std::string &answer, DtlsTransport::Fingerprint &ret) const;
    std::string AnswerMediaSection(
      const json *section, const std::string &proto, const ConnectionFactory::Connection &c) const;
    std::string AnswerAs(const std::string &proto, const SDP &client_sdp, const ConnectionFactory::Connection &c) const;
    uint32_t AssignPriority(uint32_t component_id) const;
  };
} // namespace webrtc
} // namespace base