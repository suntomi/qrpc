#include "base/loop.h"
#include "base/sig.h"
#include "base/timer.h"
#include "base/logger.h"
#include "base/http.h"
#include "base/webrtc.h"
#include "base/string.h"
#include "base/webrtc/sdp.h"
#include "nlohmann/json.hpp"

using json = nlohmann::json;
using namespace base;

struct Test3StreamContext {
    int count{0};
};
struct TestStreamContext {
    std::vector<std::string> texts;
};
bool test_webrtc_client(Loop &l, Resolver &r) {
    std::string error_msg = "";
    TestStreamContext testctx = { .texts = {"aaaa", "bbbb", "cccc"} };
    Test3StreamContext test3ctx;
    int closed = 0;
    const int MAX_RECONNECT = 2;
    webrtc::AdhocClient w(l, webrtc::ConnectionFactory::Config {
        .max_outgoing_stream_size = 32, .initial_incoming_stream_size = 32,
        .send_buffer_size = 256 * 1024,
        .http_timeout = qrpc_time_sec(5),
        .session_timeout = qrpc_time_sec(15), // udp session usally receives stun probing packet statically
        .connection_timeout = qrpc_time_sec(60),
        .fingerprint_algorithm = "sha-256",
        .resolver = r,
    }, [](webrtc::ConnectionFactory::Connection &c) {
        logger::info({{"ev","webrtc connected"}});
        c.OpenStream({.label = "test"});
        c.OpenStream({.label = "test3"});
        return QRPC_OK;
    }, [&closed, &error_msg](webrtc::ConnectionFactory::Connection &) {
        logger::info({{"ev","webrtc closed"}});
        if (closed < MAX_RECONNECT) {
            closed++;
            return qrpc_time_sec(2);
        } else {
            error_msg = "success";
            return 0ULL;
        }
    }, [&error_msg](Stream &s, const char *p, size_t sz) -> int {
        auto pl = std::string(p, sz);
        auto resp = json::parse(pl);
        logger::info({{"ev","recv data"},{"l",s.label()},{"sid",s.id()},{"pl", pl}});
        if (s.label() == "test") {
            auto now = qrpc_time_now();
            auto hello = resp["hello"].get<std::string>();
            auto count = resp["count"].get<uint64_t>();
            auto ts = resp["ts"].get<qrpc_time_t>();
            const auto &ctx = s.context<TestStreamContext>();
            auto &text = ctx.texts[count];
            if (hello != ("test:" + text)) {
                error_msg = ("stream message hello wrong: [" + hello + "] should be [", text + "]");
                return QRPC_EINVAL;
            }
            if (count < 2) {
                QRPC_LOG(info, "Data channel latency(%lld)", now - ts);
                s.Send({{"hello", ctx.texts[count + 1]},{"count",count + 1},{"ts",now}});
            } else {
                s.Close(QRPC_CLOSE_REASON_LOCAL);
            }
        } else if (s.label() == "test2") {
            error_msg = ("test2.onread should not be called");
        } else if (s.label() == "test3") {
            auto count = resp["count"].get<uint64_t>();
            s.context<Test3StreamContext>().count = count + 1;
            s.Send({{"count", s.context<Test3StreamContext>().count}});
        } else if (s.label() == "recv") {
            auto msg = resp["msg"].get<std::string>();
            if (msg != "byebye") {
                error_msg = ("Data channel3 message msg wrong: [" + msg + "] should be [byebye]");
                return QRPC_EINVAL;
            }
            s.connection().Close();
        }
        return QRPC_OK;
    }, [&closed, &testctx, &test3ctx](Stream &s) -> int {
        logger::info({{"ev","stream opened"},{"l",s.label()},{"sid",s.id()}});
        if (s.label() == "test") {
            s.SetContext(&testctx);
            return s.Send({{"hello", testctx.texts[closed]}, {"ts", qrpc_time_now()}, {"count", closed}});
        } else if (s.label() == "test2") {
            return s.Send({{"streamName", "recv"}});
        } else if (s.label() == "test3") {
            s.SetContext(&test3ctx);
            return s.Send({{"count", 0}});
        } else if (s.label() == "recv") {
            return s.Send({{"die", closed < MAX_RECONNECT}});
        }
        ASSERT(false);
        return QRPC_OK;
    }, [&error_msg](Stream &s, const Stream::CloseReason &reason) {
        logger::info({{"ev","stream closed"},{"l",s.label()},{"sid",s.id()}});
        if (s.label() == "test") {
            s.connection().OpenStream({.label = "test2"});
        } else if (s.label() == "test2") {
        } else if (s.label() == "test3") {
            if (s.context<Test3StreamContext>().count != 2) {
                logger::error({{"ev","invalid count"},{"count", s.context<Test3StreamContext>().count}});
                error_msg = ("test3.onclose count should be 2");
            }
        } else if (s.label() == "recv") {
        }
        return QRPC_OK;
    });
    if (!w.Connect("localhost", 8888)) {
        DIE("fail to start webrtc client as connect");
    }
    while (error_msg.length() <= 0) {
        l.Poll();
    }
    if (str::CmpNocase(error_msg, "success", sizeof("success") - 1) == 0) {
        return true;
    } else {
        DIE(error_msg);
        return false;
    }
    return true;
}
class Handler {
    int close_count_{0};
    const char *initial_payload_{"start"};
    const char *error_msg_{nullptr};
public:
    bool finished() const {
        return error_msg_ != nullptr;
    }
    const char *error_msg() const {
        return error_msg_;
    }
    void error(const char *msg) {
        error_msg_ = msg;
    }
    void success() {
        error_msg_ = "success";
    }
    void Reset(const char *initial_payload = "start") {
        close_count_ = 0;
        error_msg_ = nullptr;
        initial_payload_ = initial_payload;
    }
    int Connect(Session &s, std::string proto) {
        logger::info({{"ev","session connect"},{"p",proto},{"a",s.addr().str()},{"pl",std::string(initial_payload_)}});
        s.Send(initial_payload_, strlen(initial_payload_));
        return QRPC_OK;
    }
    int Read(Session &s, std::string proto, const char *p, size_t sz) {
        if (close_count_ < 1) {
            error("session should reconnect");
        } else if (close_count_ == 1){
            if (std::string(p, sz) != "start") {
                logger::error({{"ev","wrong msg"},{"msg",std::string(p, sz)}});
                error("session should receive start");
            } else {
                s.Send("die", 3);
            }
        } else if (close_count_ == 2) {
            logger::info({{"ev","close session by callback rv"}});
            return QRPC_EUSER;
        }
        return QRPC_OK;
    }
    qrpc_time_t Shutdown(Session &s, std::string proto) {
        logger::info({{"ev","session shutdown"},{"p",proto},{"a",s.addr().str()},{"reason",s.close_reason().code}});
        if (strcmp(initial_payload_, "timeout") == 0 && s.close_reason().code == QRPC_CLOSE_REASON_TIMEOUT) {
            error("timeout");
            return 0;
        }
        close_count_++;
        if (close_count_ <= 2) {
            return qrpc_time_msec(100);
        } else if (close_count_ == 3) {
            logger::info({{"ev", "reconnect test done"},{"p",proto}});
            success();
            return 0; //stop reconnection
        } else {
            DIE("should not be here");
            return 0;
        }
    }
};
class TestUdpSession : public UdpSession {
public:
    Handler &handler_;
public:
    TestUdpSession(UdpSessionFactory &f, Fd fd, const Address &a, Handler &h) : UdpSession(f, fd, a), handler_(h) {}
    int OnConnect() override { return handler_.Connect(*this, "udp"); }
    int OnRead(const char *p, size_t sz) override { return handler_.Read(*this, "udp", p, sz); }
    qrpc_time_t OnShutdown() override { return handler_.Shutdown(*this, "udp"); }
};
class TestTcpSession : public TcpSession {
public:
    Handler &handler_;
public:
    TestTcpSession(TcpSessionFactory &f, Fd fd, const Address &a, Handler &h) : TcpSession(f, fd, a), handler_(h)  {}
    int OnConnect() override { return handler_.Connect(*this, "tcp"); }
    int OnRead(const char *p, size_t sz) override { return handler_.Read(*this, "tcp", p, sz); }
    qrpc_time_t OnShutdown() override { return handler_.Shutdown(*this, "tcp"); }
};
template<class F, class S>
bool test_session(Loop &l, F &f, int port) {
    Handler h;
    logger::error({{"ev","test normal connection"}});
    f.Connect("localhost", port, [&f, &h](Fd fd, const Address &a) {
        return new S(f, fd, a, h);
    });
    while (!h.finished()) {
        l.Poll();
    }
    if (str::CmpNocase(h.error_msg(), "success", sizeof("success") - 1) != 0) {
        DIE(std::string("test normal conn error:[") + h.error_msg() + "]");
        return false;
    }
    logger::error({{"ev","test timeout connection"}});
    h.Reset("timeout");
    f.Connect("localhost", port, [&f, &h](Fd fd, const Address &a) {
        return new S(f, fd, a, h);
    });
    while (!h.finished()) {
        l.Poll();
    }
    if (str::CmpNocase(h.error_msg(), "timeout", sizeof("timeout") - 1) == 0) {
        return true;
    } else {
        DIE(std::string("test timeout error:[") + h.error_msg() + "]");
        return false;
    }
}
bool reset_test_state(Loop &l, Resolver &r) {
    const char *error_msg = nullptr;
    AdhocHttpClient hc(l, r);
    hc.Connect("localhost", 8888, [](HttpSession &s) {
        return s.Request("GET", "/reset");
    }, [&error_msg](HttpSession &s) {
        if (s.fsm().rc() != HRC_OK) {
            logger::error({{"ev","wrong response"},{"rc",s.fsm().rc()}});
            error_msg = "wrong response";
        } else {
            error_msg = "success";
        }
        return nullptr;
    });
    while (error_msg == nullptr) {
        l.Poll();
    }
    if (str::CmpNocase(error_msg, "success", sizeof("success") - 1) == 0) {
        return true;
    } else {
        DIE(error_msg);
        return false;
    }
    return true;
}
bool test_tcp_session(Loop &l, Resolver &r) {
    if (!reset_test_state(l, r)) {
        return false;
    }
    TcpClient tf(l, r, qrpc_time_sec(1));
    return test_session<TcpSessionFactory, TestTcpSession>(l, tf, 10001);
}
bool test_udp_session(Loop &l, Resolver &r, bool listen) {
    if (!reset_test_state(l, r)) {
        return false;
    }
    if (listen) {
        auto uc = UdpListener(l, [](Fd fd, const Address &a) -> Session* {
            DIE("client should not call this, provide factory via SessionFactory::Connect");
            return (Session *)nullptr;
        }, UdpListener::Config(r, qrpc_time_sec(1), 1));
        if (!uc.Bind()) {
            DIE("fail to bind");
            return false;
        }
        return test_session<UdpSessionFactory, TestUdpSession>(l, uc, 10000);
    } else {
        auto uc = UdpClient(l, r, qrpc_time_sec(1));
        return test_session<UdpSessionFactory, TestUdpSession>(l, uc, 10000);
    }
}

bool test_http_client(Loop &l, Resolver &r) {
    const char *error_msg = nullptr;
    AdhocHttpClient hc(l, r);
    hc.Connect("localhost", 8888, [](HttpSession &s) {
        return s.Request("GET", "/test");
    }, [&error_msg](HttpSession &s) {
        auto b = std::string(s.fsm().body(), s.fsm().bodylen());
        auto j = json::parse(b);
        if (j["sdp"].get<std::string>() != "hoge") {
            logger::error({{"ev","wrong response"},{"body",b}});
            error_msg = "wrong response";
        } else {
            error_msg = "success";
        }
        return nullptr;
    });
    while (error_msg == nullptr) {
        l.Poll();
    }
    if (str::CmpNocase(error_msg, "success", sizeof("success") - 1) == 0) {
        return true;
    } else {
        DIE(error_msg);
        return false;
    }
    return true;
}

bool test_address() {
    Address a;
    if (a.Set("1.2.3.4", 5678) < 0) {
        DIE("fail to set address");
    }
    if (a.port() != 5678) {
        DIE("fail to get port");
    }
    if (a.hostip() != "1.2.3.4") {
        DIE("fail to get hostip");
    }
    if (a.str() != "1.2.3.4:5678") {
        DIE("fail to get str");
    }
    return true;
}

bool test_sdp() {
auto sdp = R"sdp(
v=0
o=- 2755857735278132688 2 IN IP4 127.0.0.1
s=-
t=0 0
a=group:BUNDLE 0 1
a=extmap-allow-mixed
a=msid-semantic: WMS e1c6c32a-0d3d-4c1f-b8e3-38fc1382e07a
m=video 9 UDP/TLS/RTP/SAVPF 96 97 102 103 104 105 106 107 108 109 127 125 39 40 45 46 98 99 100 101 112 113 116 117 118
c=IN IP4 0.0.0.0
a=rtcp:9 IN IP4 0.0.0.0
a=ice-ufrag:4mR1
a=ice-pwd:d7Ou6mHlq1enMEWs0v0VAYov
a=ice-options:trickle
a=fingerprint:sha-256 F5:CE:1E:60:EC:B1:A4:29:DC:C4:2A:53:A4:DA:A0:9F:A3:3B:6D:11:FE:8C:7F:48:5F:03:AD:1A:7B:76:69:67
a=setup:actpass
a=mid:0
a=extmap:1 urn:ietf:params:rtp-hdrext:toffset
a=extmap:2 http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time
a=extmap:3 urn:3gpp:video-orientation
a=extmap:4 http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01
a=extmap:5 http://www.webrtc.org/experiments/rtp-hdrext/playout-delay
a=extmap:6 http://www.webrtc.org/experiments/rtp-hdrext/video-content-type
a=extmap:7 http://www.webrtc.org/experiments/rtp-hdrext/video-timing
a=extmap:8 http://www.webrtc.org/experiments/rtp-hdrext/color-space
a=extmap:9 urn:ietf:params:rtp-hdrext:sdes:mid
a=extmap:10 urn:ietf:params:rtp-hdrext:sdes:rtp-stream-id
a=extmap:11 urn:ietf:params:rtp-hdrext:sdes:repaired-rtp-stream-id
a=extmap:12 https://aomediacodec.github.io/av1-rtp-spec/#dependency-descriptor-rtp-header-extension
a=extmap:14 http://www.webrtc.org/experiments/rtp-hdrext/video-layers-allocation00
a=sendonly
a=msid:e1c6c32a-0d3d-4c1f-b8e3-38fc1382e07a e29c78af-7d82-48f1-b1ad-c08c427c7a74
a=rtcp-mux
a=rtcp-rsize
a=rtpmap:96 VP8/90000
a=rtcp-fb:96 goog-remb
a=rtcp-fb:96 transport-cc
a=rtcp-fb:96 ccm fir
a=rtcp-fb:96 nack
a=rtcp-fb:96 nack pli
a=rtpmap:97 rtx/90000
a=fmtp:97 apt=96
a=rtpmap:102 H264/90000
a=rtcp-fb:102 goog-remb
a=rtcp-fb:102 transport-cc
a=rtcp-fb:102 ccm fir
a=rtcp-fb:102 nack
a=rtcp-fb:102 nack pli
a=fmtp:102 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42001f
a=rtpmap:103 rtx/90000
a=fmtp:103 apt=102
a=rtpmap:104 H264/90000
a=rtcp-fb:104 goog-remb
a=rtcp-fb:104 transport-cc
a=rtcp-fb:104 ccm fir
a=rtcp-fb:104 nack
a=rtcp-fb:104 nack pli
a=fmtp:104 level-asymmetry-allowed=1;packetization-mode=0;profile-level-id=42001f
a=rtpmap:105 rtx/90000
a=fmtp:105 apt=104
a=rtpmap:106 H264/90000
a=rtcp-fb:106 goog-remb
a=rtcp-fb:106 transport-cc
a=rtcp-fb:106 ccm fir
a=rtcp-fb:106 nack
a=rtcp-fb:106 nack pli
a=fmtp:106 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f
a=rtpmap:107 rtx/90000
a=fmtp:107 apt=106
a=rtpmap:108 H264/90000
a=rtcp-fb:108 goog-remb
a=rtcp-fb:108 transport-cc
a=rtcp-fb:108 ccm fir
a=rtcp-fb:108 nack
a=rtcp-fb:108 nack pli
a=fmtp:108 level-asymmetry-allowed=1;packetization-mode=0;profile-level-id=42e01f
a=rtpmap:109 rtx/90000
a=fmtp:109 apt=108
a=rtpmap:127 H264/90000
a=rtcp-fb:127 goog-remb
a=rtcp-fb:127 transport-cc
a=rtcp-fb:127 ccm fir
a=rtcp-fb:127 nack
a=rtcp-fb:127 nack pli
a=fmtp:127 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=4d001f
a=rtpmap:125 rtx/90000
a=fmtp:125 apt=127
a=rtpmap:39 H264/90000
a=rtcp-fb:39 goog-remb
a=rtcp-fb:39 transport-cc
a=rtcp-fb:39 ccm fir
a=rtcp-fb:39 nack
a=rtcp-fb:39 nack pli
a=fmtp:39 level-asymmetry-allowed=1;packetization-mode=0;profile-level-id=4d001f
a=rtpmap:40 rtx/90000
a=fmtp:40 apt=39
a=rtpmap:45 AV1/90000
a=rtcp-fb:45 goog-remb
a=rtcp-fb:45 transport-cc
a=rtcp-fb:45 ccm fir
a=rtcp-fb:45 nack
a=rtcp-fb:45 nack pli
a=fmtp:45 level-idx=5;profile=0;tier=0
a=rtpmap:46 rtx/90000
a=fmtp:46 apt=45
a=rtpmap:98 VP9/90000
a=rtcp-fb:98 goog-remb
a=rtcp-fb:98 transport-cc
a=rtcp-fb:98 ccm fir
a=rtcp-fb:98 nack
a=rtcp-fb:98 nack pli
a=fmtp:98 profile-id=0
a=rtpmap:99 rtx/90000
a=fmtp:99 apt=98
a=rtpmap:100 VP9/90000
a=rtcp-fb:100 goog-remb
a=rtcp-fb:100 transport-cc
a=rtcp-fb:100 ccm fir
a=rtcp-fb:100 nack
a=rtcp-fb:100 nack pli
a=fmtp:100 profile-id=2
a=rtpmap:101 rtx/90000
a=fmtp:101 apt=100
a=rtpmap:112 H264/90000
a=rtcp-fb:112 goog-remb
a=rtcp-fb:112 transport-cc
a=rtcp-fb:112 ccm fir
a=rtcp-fb:112 nack
a=rtcp-fb:112 nack pli
a=fmtp:112 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=64001f
a=rtpmap:113 rtx/90000
a=fmtp:113 apt=112
a=rtpmap:116 red/90000
a=rtpmap:117 rtx/90000
a=fmtp:117 apt=116
a=rtpmap:118 ulpfec/90000
a=rid:h send
a=rid:m send
a=rid:l send
a=rid:r recv
a=simulcast:send h;m;l recv r
m=application 9 UDP/DTLS/SCTP webrtc-datachannel
c=IN IP4 0.0.0.0
a=ice-ufrag:4mR1
a=ice-pwd:d7Ou6mHlq1enMEWs0v0VAYov
a=ice-options:trickle
a=fingerprint:sha-256 F5:CE:1E:60:EC:B1:A4:29:DC:C4:2A:53:A4:DA:A0:9F:A3:3B:6D:11:FE:8C:7F:48:5F:03:AD:1A:7B:76:69:67
a=setup:actpass
a=mid:1
a=sctp-port:5000
a=max-message-size:262144
})sdp";
    auto s = webrtc::SDP(sdp);
    QRPC_LOGJ(info, s);
    ASSERT(false);
    return true;
}

int main(int argc, char *argv[]) {
    bool alive = true;
    Loop l;
    if (l.Open(1024) < 0) {
        DIE("fail to init loop");
    }
    AsyncResolver ares(l);
    if (!ares.Initialize()) {
        DIE("fail to init ares");
    }
    SignalHandler sh;
    // in here, return type annotation is required for making compiler happy
    if (!sh.Init(l, [&alive](SignalHandler &s) -> SignalHandler & {
        return s.Ignore(SIGPIPE)
        .Handle(SIGINT, [&alive](int sig, const Signal &s) {
            logger::info("SIGINT");
            alive = false;
        })
        .Handle(SIGTERM, [](int sig, const Signal &s) {
            logger::info("SIGTERM");
            exit(0);
        });
    })) {
        DIE("fail to setup signal handler");
    }
    TRACE("======== test_sdp ========");
    if (!test_sdp()) {
        return 1;
    }
    TRACE("======== test_webrtc_client ========");
    if (!test_webrtc_client(l, ares)) {
        return 1;
    }
    TRACE("======== test_address ========");
    if (!test_address()) {
        return 1;
    }
    TRACE("======== test_udp_session (client) ========");
    if (!test_udp_session(l, ares, false)) {
        return 1;
    }
    TRACE("======== test_udp_session (server) ========");
    if (!test_udp_session(l, ares, true)) {
        return 1;
    }
    TRACE("======== test_tcp_session ========");
    if (!test_tcp_session(l, ares)) {
        return 1;
    }
    TRACE("======== test_http_client ========");
    if (!test_http_client(l, ares)) {
        return 1;
    }
    return 0;
}
