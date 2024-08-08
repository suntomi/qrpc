#pragma once

#include <stdint.h>
#include <string>

#include "base/webrtc.h"

#include "sdptransform.hpp"
#include <nlohmann/json.hpp>
using json = nlohmann::json;

// chrome
// client -> server offer
// v=0
// o=- 2755857735278132688 2 IN IP4 127.0.0.1
// s=-
// t=0 0
// a=group:BUNDLE 0 1
// a=extmap-allow-mixed
// a=msid-semantic: WMS e1c6c32a-0d3d-4c1f-b8e3-38fc1382e07a
// m=video 9 UDP/TLS/RTP/SAVPF 96 97 102 103 104 105 106 107 108 109 127 125 39 40 45 46 98 99 100 101 112 113 116 117 118
// c=IN IP4 0.0.0.0
// a=rtcp:9 IN IP4 0.0.0.0
// a=ice-ufrag:4mR1
// a=ice-pwd:d7Ou6mHlq1enMEWs0v0VAYov
// a=ice-options:trickle
// a=fingerprint:sha-256 F5:CE:1E:60:EC:B1:A4:29:DC:C4:2A:53:A4:DA:A0:9F:A3:3B:6D:11:FE:8C:7F:48:5F:03:AD:1A:7B:76:69:67
// a=setup:actpass
// a=mid:0
// a=extmap:1 urn:ietf:params:rtp-hdrext:toffset
// a=extmap:2 http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time
// a=extmap:3 urn:3gpp:video-orientation
// a=extmap:4 http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01
// a=extmap:5 http://www.webrtc.org/experiments/rtp-hdrext/playout-delay
// a=extmap:6 http://www.webrtc.org/experiments/rtp-hdrext/video-content-type
// a=extmap:7 http://www.webrtc.org/experiments/rtp-hdrext/video-timing
// a=extmap:8 http://www.webrtc.org/experiments/rtp-hdrext/color-space
// a=extmap:9 urn:ietf:params:rtp-hdrext:sdes:mid
// a=extmap:10 urn:ietf:params:rtp-hdrext:sdes:rtp-stream-id
// a=extmap:11 urn:ietf:params:rtp-hdrext:sdes:repaired-rtp-stream-id
// a=extmap:12 https://aomediacodec.github.io/av1-rtp-spec/#dependency-descriptor-rtp-header-extension
// a=extmap:14 http://www.webrtc.org/experiments/rtp-hdrext/video-layers-allocation00
// a=sendonly
// a=msid:e1c6c32a-0d3d-4c1f-b8e3-38fc1382e07a e29c78af-7d82-48f1-b1ad-c08c427c7a74
// a=rtcp-mux
// a=rtcp-rsize
// a=rtpmap:96 VP8/90000
// a=rtcp-fb:96 goog-remb
// a=rtcp-fb:96 transport-cc
// a=rtcp-fb:96 ccm fir
// a=rtcp-fb:96 nack
// a=rtcp-fb:96 nack pli
// a=rtpmap:97 rtx/90000
// a=fmtp:97 apt=96
// a=rtpmap:102 H264/90000
// a=rtcp-fb:102 goog-remb
// a=rtcp-fb:102 transport-cc
// a=rtcp-fb:102 ccm fir
// a=rtcp-fb:102 nack
// a=rtcp-fb:102 nack pli
// a=fmtp:102 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42001f
// a=rtpmap:103 rtx/90000
// a=fmtp:103 apt=102
// a=rtpmap:104 H264/90000
// a=rtcp-fb:104 goog-remb
// a=rtcp-fb:104 transport-cc
// a=rtcp-fb:104 ccm fir
// a=rtcp-fb:104 nack
// a=rtcp-fb:104 nack pli
// a=fmtp:104 level-asymmetry-allowed=1;packetization-mode=0;profile-level-id=42001f
// a=rtpmap:105 rtx/90000
// a=fmtp:105 apt=104
// a=rtpmap:106 H264/90000
// a=rtcp-fb:106 goog-remb
// a=rtcp-fb:106 transport-cc
// a=rtcp-fb:106 ccm fir
// a=rtcp-fb:106 nack
// a=rtcp-fb:106 nack pli
// a=fmtp:106 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f
// a=rtpmap:107 rtx/90000
// a=fmtp:107 apt=106
// a=rtpmap:108 H264/90000
// a=rtcp-fb:108 goog-remb
// a=rtcp-fb:108 transport-cc
// a=rtcp-fb:108 ccm fir
// a=rtcp-fb:108 nack
// a=rtcp-fb:108 nack pli
// a=fmtp:108 level-asymmetry-allowed=1;packetization-mode=0;profile-level-id=42e01f
// a=rtpmap:109 rtx/90000
// a=fmtp:109 apt=108
// a=rtpmap:127 H264/90000
// a=rtcp-fb:127 goog-remb
// a=rtcp-fb:127 transport-cc
// a=rtcp-fb:127 ccm fir
// a=rtcp-fb:127 nack
// a=rtcp-fb:127 nack pli
// a=fmtp:127 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=4d001f
// a=rtpmap:125 rtx/90000
// a=fmtp:125 apt=127
// a=rtpmap:39 H264/90000
// a=rtcp-fb:39 goog-remb
// a=rtcp-fb:39 transport-cc
// a=rtcp-fb:39 ccm fir
// a=rtcp-fb:39 nack
// a=rtcp-fb:39 nack pli
// a=fmtp:39 level-asymmetry-allowed=1;packetization-mode=0;profile-level-id=4d001f
// a=rtpmap:40 rtx/90000
// a=fmtp:40 apt=39
// a=rtpmap:45 AV1/90000
// a=rtcp-fb:45 goog-remb
// a=rtcp-fb:45 transport-cc
// a=rtcp-fb:45 ccm fir
// a=rtcp-fb:45 nack
// a=rtcp-fb:45 nack pli
// a=fmtp:45 level-idx=5;profile=0;tier=0
// a=rtpmap:46 rtx/90000
// a=fmtp:46 apt=45
// a=rtpmap:98 VP9/90000
// a=rtcp-fb:98 goog-remb
// a=rtcp-fb:98 transport-cc
// a=rtcp-fb:98 ccm fir
// a=rtcp-fb:98 nack
// a=rtcp-fb:98 nack pli
// a=fmtp:98 profile-id=0
// a=rtpmap:99 rtx/90000
// a=fmtp:99 apt=98
// a=rtpmap:100 VP9/90000
// a=rtcp-fb:100 goog-remb
// a=rtcp-fb:100 transport-cc
// a=rtcp-fb:100 ccm fir
// a=rtcp-fb:100 nack
// a=rtcp-fb:100 nack pli
// a=fmtp:100 profile-id=2
// a=rtpmap:101 rtx/90000
// a=fmtp:101 apt=100
// a=rtpmap:112 H264/90000
// a=rtcp-fb:112 goog-remb
// a=rtcp-fb:112 transport-cc
// a=rtcp-fb:112 ccm fir
// a=rtcp-fb:112 nack
// a=rtcp-fb:112 nack pli
// a=fmtp:112 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=64001f
// a=rtpmap:113 rtx/90000
// a=fmtp:113 apt=112
// a=rtpmap:116 red/90000
// a=rtpmap:117 rtx/90000
// a=fmtp:117 apt=116
// a=rtpmap:118 ulpfec/90000
// a=rid:h send
// a=rid:m send
// a=rid:l send
// a=simulcast:send h;m;l
// m=application 9 UDP/DTLS/SCTP webrtc-datachannel
// c=IN IP4 0.0.0.0
// a=ice-ufrag:4mR1
// a=ice-pwd:d7Ou6mHlq1enMEWs0v0VAYov
// a=ice-options:trickle
// a=fingerprint:sha-256 F5:CE:1E:60:EC:B1:A4:29:DC:C4:2A:53:A4:DA:A0:9F:A3:3B:6D:11:FE:8C:7F:48:5F:03:AD:1A:7B:76:69:67
// a=setup:actpass
// a=mid:1
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
    bool Answer(ConnectionFactory::Connection &c, std::string &answer) const;
    static int Offer(const ConnectionFactory::Connection &c, 
      const std::string &ufrag, const std::string &pwd, std::string &offer);
  public:
    std::vector<Candidate> Candidates() const;
    bool FindMediaSection(const std::string &type, json &j) const;
  protected:
    bool GetRemoteFingerPrint(const json &section, std::string &answer, DtlsTransport::Fingerprint &ret) const;
    bool AnswerMediaSection(
      const json &section, const std::string &proto, ConnectionFactory::Connection &c,
      std::string &answer, std::string &mid) const;
    uint32_t AssignPriority(uint32_t component_id) const;
  };
} // namespace webrtc
} // namespace base