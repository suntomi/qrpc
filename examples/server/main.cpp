#include "base/loop.h"
#include "base/sig.h"
#include "base/timer.h"
#include "base/logger.h"
#include "base/http.h"
#include "base/webrtc.h"
#include "base/string.h"
#include "nlohmann/json.hpp"

using json = nlohmann::json;
using Ticker = base::Timer;
using namespace base;

bool SetupSignalHandler(SignalHandler &sh, Loop &l) {
    return sh.Ignore(SIGPIPE)
        .Handle(SIGINT, [](int sig, const Signal &s) {
            logger::info("SIGINT");
            exit(0);
        })
        .Handle(SIGTERM, [](int sig, const Signal &s) {
            logger::info("SIGTERM");
            exit(0);
        })
        .Handle(SIGCHLD, [](int sig, const Signal &s) {
            logger::info("SIGCHLD");
        })
        .Handle(SIGUSR1, [](int sig, const Signal &s) {
            logger::info("SIGUSR1");
        })
        .Handle(SIGUSR2, [](int sig, const Signal &s) {
            logger::info("SIGUSR2");
        }).Start(l);
}

int main(int argc, char *argv[]) {
    Loop l;
    SignalHandler sh;
    Ticker t(qrpc_time_msec(10)); // 10ms
    if (l.Open(1024) < 0) {
        logger::error("fail to init loop");
        exit(1);
    }
    if (!SetupSignalHandler(sh, l)) {
        logger::error("fail to setup signal handler");
        exit(1);
    }
    if (t.Init(l) < 0) {
        logger::error("fail to start timer");
        exit(1);
    }
    HttpServer s(l);
    WebRTCServer w(l, &t, {
        {.protocol = WebRTCServer::Config::UDP, .ip = "", .port = 9999, .priority = 1},
        {.protocol = WebRTCServer::Config::TCP, .ip = "", .port = 9999, .priority = 100}
    });
    if (w.Init() < 0) {
        logger::error("fail to init webrtc");
        exit(1);
    }
    HttpRouter r = HttpRouter().
    Route(RGX("/accept"), [&w](HttpSession &s) {
        int r;
        std::string sdp;
        if ((r = w.NewConnection(s.fsm().body(), sdp)) < 0) {
            logger::error("fail to create connection");
            s.ServerError("server error %s", r);
        }
        std::string sdplen = std::to_string(sdp.length());
        HttpHeader h[] = {
            {.key = "Content-Type", .val = "application/json"},
            {.key = "Content-Length", .val = sdplen.c_str()}
        };
        s.Write(HRC_OK, h, 2, sdp.c_str(), sdp.length());
	    return nullptr;
    }).
    Route(RGX("/test"), [](HttpSession &s) {
        json j = {
            {"sdp", "hoge"}
        };
        std::string body = j.dump();
	    std::string bodylen = std::to_string(body.length());
        HttpHeader h[] = {
            {.key = "Content-Type", .val = "application/json"},
            {.key = "Content-Length", .val = bodylen.c_str()}
        };
        s.Write(HRC_OK, h, 2, body.c_str(), body.length());
	    return nullptr;
    }).
    Route(RGX("/ws"), [](HttpSession &s) {
        return WebSocketServer::Upgrade(s, [](WebSocketSession &ws, const char *p, size_t sz) {
            // echo server
            return ws.Send(p, sz);
        });
    });
    if (!s.Listen(8888, r)) {
        logger::error("fail to listen");
        exit(1);
    }
    UdpListener::Config c = { .alarm_processor = &t, .session_timeout_sec = 5};
    AdhocUdpServer us(l, [](AdhocUdpSession &s, const char *p, size_t sz) {
        // echo udp
        logger::info({{"ev","recv packet"},{"a",s.addr().str()},{"pl", std::string(p, sz)}});
        return s.Send(p, sz);
    }, &c);
    if (!us.Listen(9999)) {
        logger::error("fail to listen");
        exit(1);
    }
    while (true) {
        l.Poll();
    }
    return 0;
}
