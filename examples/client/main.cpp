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

static const char *error_msg = nullptr;
static void clear_error() {
    error_msg = nullptr;
}
static void error(const char *msg) {
    error_msg = msg;
}
static void success() {
    error_msg = "success";
}
bool test_webrtc_client(Loop &l, AlarmProcessor &ap) {
    webrtc::AdhocClient w(l, webrtc::ConnectionFactory::Config {
        .max_outgoing_stream_size = 32, .initial_incoming_stream_size = 32,
        .send_buffer_size = 256 * 1024,
        .udp_session_timeout = qrpc_time_sec(15), // udp session usally receives stun probing packet statically
        .connection_timeout = qrpc_time_sec(60),
        .fingerprint_algorithm = "sha-256",
        .alarm_processor = ap,
    }, [](webrtc::ConnectionFactory::Connection &) {
        return QRPC_OK;
    }, [](webrtc::ConnectionFactory::Connection &) {
    }, [](Stream &s, const char *p, size_t sz) {
        auto pl = std::string(p, sz);
        logger::info({{"ev","recv dc packet"},{"l",s.label()},{"sid",s.id()},{"pl", pl}});
        return 0;
    }, [](Stream &s) {
        logger::info({{"ev","stream opened"},{"l",s.label()},{"sid",s.id()}});
        return QRPC_OK;
    }, [](Stream &s, const Stream::CloseReason &reason) {
        logger::info({{"ev","stream closed"},{"l",s.label()},{"sid",s.id()}});
    });
    base::Client &bcl = w;
    if (!bcl.Connect("localhost", 8888)) {
        DIE("fail to start webrtc client");
    }
    while (error_msg == nullptr) {
        l.PollAres();
    }
    if (str::CmpNocase(error_msg, "success", sizeof("success") - 1)) {
        return true;
    } else {
        DIE(error_msg);
        return false;
    }
    return true;
}
class Handler {
public:
    static int Connect(Session &s, std::string proto) {
        logger::info({{"ev","session connect"},{"p",proto},{"a",s.addr().str()}});
        return s.Send("start", 5);
    }
    static int Read(Session &s, std::string proto, int &close_count, const char *p, size_t sz) {
        if (close_count < 1) {
            error("session should reconnect");
        } else if (close_count == 1){
            if (std::string(p, sz) != "start") {
                logger::error({{"ev","wrong msg"},{"msg",std::string(p, sz)}});
                error("session should receive start");
            } else {
                s.Send("die", 3);
            }
        } else if (close_count == 2) {
            logger::info({{"ev","close session by callback rv"}});
            return QRPC_EUSER;
        }
        return QRPC_OK;
    }
    static qrpc_time_t Shutdown(Session &s, std::string proto, int &close_count) {
        logger::info({{"ev","session shutdown"},{"p",proto},{"a",s.addr().str()}});
        close_count++;
        if (close_count <= 2) {
            return qrpc_time_msec(100);
        } else if (close_count == 3) {
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
    int close_count_{0};
public:
    TestUdpSession(UdpSessionFactory &f, Fd fd, const Address &a) : UdpSession(f, fd, a) {}
    int OnConnect() override { return Handler::Connect(*this, "udp"); }
    int OnRead(const char *p, size_t sz) override { return Handler::Read(*this, "udp", close_count_, p, sz); }
    qrpc_time_t OnShutdown() override { return Handler::Shutdown(*this, "udp", close_count_); }
};
class TestTcpSession : public TcpSession {
public:
    int close_count_{0};
public:
    TestTcpSession(TcpSessionFactory &f, Fd fd, const Address &a) : TcpSession(f, fd, a) {}
    int OnConnect() override { return Handler::Connect(*this, "tcp"); }
    int OnRead(const char *p, size_t sz) override { return Handler::Read(*this, "tcp", close_count_, p, sz); }
    qrpc_time_t OnShutdown() override { return Handler::Shutdown(*this, "tcp", close_count_); }
};
bool test_udp_session(Loop &l, AlarmProcessor &ap) {
    clear_error();
    UdpSessionFactory uf(l, ap, qrpc_time_sec(1));
    if (!uf.Bind()) {
        DIE("fail to bind");
    }
    uf.Connect("localhost", 10000, [&uf](Fd fd, const Address &a) {
        return new TestUdpSession(uf, fd, a);
    });
    while (error_msg == nullptr) {
        l.PollAres();
    }
    if (str::CmpNocase(error_msg, "success", sizeof("success") - 1) == 0) {
        return true;
    } else {
        DIE(error_msg);
        return false;
    }
    return true;
}
bool test_tcp_session(Loop &l, AlarmProcessor &ap) {
    clear_error();
    TcpSessionFactory tf(l, ap);
    tf.Connect("localhost", 10001, [&tf](Fd fd, const Address &a) {
        return new TestTcpSession(tf, fd, a);
    });
    while (error_msg == nullptr) {
        l.PollAres();
    }
    if (str::CmpNocase(error_msg, "success", sizeof("success") - 1) == 0) {
        return true;
    } else {
        DIE(error_msg);
        return false;
    }
    return true;
}
bool test_http_client(Loop &l, AlarmProcessor &ap) {
    clear_error();
    AdhocHttpClient hc(l, ap);
    hc.Connect("localhost", 8888, [](HttpSession &s) {
        return s.Request("GET", "/test");
    }, [](HttpSession &s) {
        auto b = std::string(s.fsm().body(), s.fsm().bodylen());
        auto j = json::parse(b);
        if (j["sdp"].get<std::string>() != "hoge") {
            logger::error({{"ev","wrong response"},{"body",b}});
            error("wrong response");
        } else {
            success();
        }
        return nullptr;
    });
    while (error_msg == nullptr) {
        l.PollAres();
    }
    if (str::CmpNocase(error_msg, "success", sizeof("success") - 1) == 0) {
        return true;
    } else {
        DIE(error_msg);
        return false;
    }
    return true;
}

int main(int argc, char *argv[]) {
    bool alive = true;
    Loop l;
    if (l.Open(1024) < 0) {
        DIE("fail to init loop");
    }
    if (!l.ares().Initialize()) {
        DIE("fail to init resolver");
    }
    TimerScheduler t(qrpc_time_msec(300));
    if (t.Init(l) < 0) {
        DIE("fail to start timer");
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
    if (!test_http_client(l, t)) {
        return 1;
    }
    if (!test_udp_session(l, t)) {
        return 1;
    }
    if (!test_tcp_session(l, t)) {
        return 1;
    }
    // if (!test_webrtc_client()) {
    //     return 1;
    // }
    return 0;
}
