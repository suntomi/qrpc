#include "base/loop.h"
#include "base/sig.h"
#include "base/timer.h"
#include "base/logger.h"
#include "base/http.h"
#include "base/string.h"
#include "nlohmann/json.hpp"

using json = nlohmann::json;
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
    Timer t(qrpc_time_sec(1)); // 1 sec
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
    HttpRouter r;
    r.Route(RGX("/accept"), [](HttpSession &s) {
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
    }).Route(RGX("/ws"), [](HttpSession &s) {
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
        logger::info({{"ev","recv packet"}, {"pl", std::string(p, sz)}});
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
