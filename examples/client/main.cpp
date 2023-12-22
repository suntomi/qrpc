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

int main(int argc, char *argv[]) {
    bool alive = true;
    Loop l; {
    if (l.Open(1024) < 0) {
        DIE("fail to init loop");
    }
    TimerScheduler t(qrpc_time_msec(10)); { // 10ms resolution
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
    webrtc::AdhocClient w(l, webrtc::ConnectionFactory::Config {
        .max_outgoing_stream_size = 32, .initial_incoming_stream_size = 32,
        .send_buffer_size = 256 * 1024,
        .udp_session_timeout = qrpc_time_sec(15), // udp session usally receives stun probing packet statically
        .connection_timeout = qrpc_time_sec(60),
        .fingerprint_algorithm = "sha-256",
        .alarm_processor = t,
    }, [](webrtc::ConnectionFactory::Connection &) {
        return QRPC_OK;
    }, [](webrtc::ConnectionFactory::Connection &) {
    }, [](Stream &s, const char *p, size_t sz) {
        auto pl = std::string(p, sz);
        logger::info({{"ev","recv dc packet"},{"l",s.label()},{"sid",s.id()},{"pl", pl}});
        auto req = json::parse(pl);
        if (s.label() == "test") {
            // echo + label name
            return s.Send({
                {"hello", s.label() + ":" + req["hello"].get<std::string>()},
                {"ts", req["ts"].get<uint64_t>()},
                {"count", req["count"].get<uint64_t>()}
            }); // echo
        } else if (s.label() == "test2") {
            auto stream_name = req["streamName"].get<std::string>();
            auto ns = s.connection().OpenStream({
                .label = stream_name
            });
            ASSERT(ns != nullptr);
        } else if (s.label() == "test3") {
            auto count = req["count"].get<uint64_t>();
            if (count >= 2) {
                s.Close(QRPC_CLOSE_REASON_LOCAL, 0, "byebye");
            } else {
                return s.Send({{"count", count}});
            }
        } else if (s.label() == "recv") {
            auto die = req["die"].get<bool>();
            if (die) {
                logger::info({{"ev","recv die"}});
                s.connection().Close();
            } else {
                return s.Send({{"msg", "byebye"}});
            }
        }
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
    while (alive) {
        l.Poll();
    }
    }}
    return 0;
}
