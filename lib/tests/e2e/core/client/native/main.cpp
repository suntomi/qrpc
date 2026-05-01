#include "base/loop.h"
#include "base/sig.h"
#include "base/timer.h"
#include "base/logger.h"
#include "base/http.h"
#include "base/webrtc.h"
#include "base/string.h"
#include "base/webrtc/sdp.h"
#include "json.hpp"
#include "test_sdp_data.h"
#include <algorithm>
#include <cstring>
#include <functional>
#include <vector>

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
    bool secure = false;
    const int MAX_RECONNECT = 2;
    auto client_config = base::webrtc::Client::Config::From(
        r, qrpc_time_sec(30), qrpc_time_sec(10));
    base::webrtc::AdhocClient w(l, std::move(client_config), [](base::webrtc::Client::Connection &c) -> int {
        // connection::onopen
        logger::info({{"ev","webrtc connected"}});
        c.OpenStream({.label = "test"});
        c.OpenStream({.label = "test3"});
        return QRPC_OK;
    }, [&closed, &error_msg](
        base::webrtc::Client::Connection &,
        const base::webrtc::ConnectionFactory::CloseReason &reason
    ) -> qrpc_time_t {
        // connection::onclose
        logger::info({{"ev","webrtc closed"},{"code",reason.code},{"detail_code",reason.detail_code},{"msg",reason.msg},{"closed",closed}});
        if (reason.code == QRPC_CLOSE_REASON_PROTOCOL) {
            error_msg = "protocol error:" + reason.msg;
            return qrpc_alarm_stop_rv();
        }
        if (closed < MAX_RECONNECT) {
            closed++;
            return qrpc_time_sec(2);
        } else {
            error_msg = "success";
            return qrpc_alarm_stop_rv();
        }
    }, [&error_msg](Stream &s, const char *p, size_t sz) -> int {
        // stream::onread
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
                error_msg = ("stream message hello wrong: [" + hello + "] should be [" + text + "]");
                return QRPC_EINVAL;
            }
            if (count < 2) {
                QRPC_LOG(info, "Data channel latency(%lld)", now - ts);
                s.Send({{"hello", ctx.texts[count + 1]},{"count",count + 1},{"ts",now}});
            } else {
                s.Close(QRPC_CLOSE_REASON_LOCAL);
            }
            return QRPC_OK;
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
        // stream::onopen
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
        // stream::onclose
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
    });
    if (!w.Connect({
        .host = "localhost", .port = 8888
    }, {
        .rtp = {
            .initial_outgoing_bitrate = 10000000,
            .max_outgoing_bitrate = 0,
            .max_incoming_bitrate = 10000000,
            .min_outgoing_bitrate = 0,
        },
        .params = {
            .max_outgoing_stream_size = 32,
            .initial_incoming_stream_size = 32,
            .send_buffer_size = 256 * 1024,
            .session_timeout = qrpc_time_sec(15), // udp session usally receives stun probing packet statically
            .connection_timeout = qrpc_time_sec(60),
            .shutdown_timeout = qrpc_time_sec(3),
            .http_timeout = qrpc_time_sec(5),
            .consent_check_interval = qrpc_time_sec(10),
            .fingerprint_algorithm = "sha-256",
        },
        .certpair = secure ? std::optional(CertificatePair::Default()) : std::nullopt
    })) {
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
template<class P>
class TestUdpSession : public P {
public:
    Handler &handler_;
public:
    TestUdpSession(SessionFactory &f, Fd fd, const Address &a, Handler &h) : P(f, fd, a), handler_(h) {}
    int OnConnect() override { return handler_.Connect(*this, "udp"); }
    int OnRead(const char *p, size_t sz) override { return handler_.Read(*this, "udp", p, sz); }
    qrpc_time_t OnShutdown() override { return handler_.Shutdown(*this, "udp"); }
};
class TestTcpSession : public TcpClientSession {
public:
    Handler &handler_;
public:
    TestTcpSession(SessionFactory &f, Fd fd, const Address &a, Handler &h) : TcpClientSession(f, fd, a), handler_(h)  {}
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
    hc.Connect("localhost", 8888, [](HttpClientSession &s) {
        return s.Request("GET", "/reset");
    }, [&error_msg](HttpClientSession &s) {
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
    return test_session<TcpClientSessionFactory, TestTcpSession>(l, tf, 10001);
}
bool test_udp_session(Loop &l, Resolver &r) {
    if (!reset_test_state(l, r)) {
        return false;
    }

    auto uc = UdpClient(l, r, qrpc_time_sec(1));
    return test_session<UdpClientSessionFactory, TestUdpSession<UdpClient::UdpSession>>(l, uc, 10000);
}

bool test_http_client(Loop &l, Resolver &r) {
    const char *error_msg = nullptr;
    AdhocHttpClient hc(l, r);
    hc.Connect("localhost", 8888, [](HttpClientSession &s) {
        return s.Request("GET", "/test");
    }, [&error_msg](HttpClientSession &s) {
        auto b = s.fsm().body();
        try {
            auto j = json::parse(b);
            if (j["sdp"].get<std::string>() != "hoge") {
                logger::error({{"ev","wrong response"},{"body",b}});
                error_msg = "wrong response";
            } else {
                error_msg = "success";
            }
        } catch (const json::parse_error &e) {
            logger::error({{"ev","json parse error"},{"msg",e.what()},{"body",b}});
            error_msg = "json parse error";
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
    auto s = base::webrtc::SDP(ffsdp);
    QRPC_LOGJ(info, s);
    auto s2 = base::webrtc::SDP(sdp);
    QRPC_LOGJ(info, s2);
    // ASSERT(false);
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

    struct TestSuite {
        const char *name;
        std::function<bool()> run;
    };
    std::vector<TestSuite> suites = {
        {"sdp", []() { return test_sdp(); }},
        {"webrtc_client", [&l, &ares]() { return test_webrtc_client(l, ares); }},
        {"address", []() { return test_address(); }},
        {"udp_session", [&l, &ares]() { return test_udp_session(l, ares); }},
        {"tcp_session", [&l, &ares]() { return test_tcp_session(l, ares); }},
        {"http_client", [&l, &ares]() { return test_http_client(l, ares); }},
    };

    const char *requested_suite = argc > 1 ? argv[1] : nullptr;
    if (requested_suite != nullptr) {
        auto it = std::find_if(suites.begin(), suites.end(), [requested_suite](const auto &suite) {
            return strcmp(suite.name, requested_suite) == 0;
        });
        if (it == suites.end()) {
            logger::error({{"ev","unknown test suite"},{"suite",requested_suite}});
            return 1;
        }
        TRACE("======== test_%s ========", it->name);
        return it->run() ? 0 : 1;
    }

    for (const auto &suite : suites) {
        TRACE("======== test_%s ========", suite.name);
        if (!suite.run()) {
            return 1;
        }
    }
    return 0;
}
