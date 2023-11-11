#include "base/loop.h"
#include "base/sig.h"
#include "base/logger.h"
#include "base/http.h"
#include "base/string.h"
#include "nlohmann/json.hpp"

using json = nlohmann::json;
using namespace base;

int SetupSignalHandler(SignalHandler &sh, Loop &l) {
    sh.Ignore(SIGPIPE)
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
        });
    return l.Add(sh.fd(), &sh, Loop::EV_READ);
}

int main(int argc, char *argv[]) {
    Loop l;
    SignalHandler sh;
    if (l.Open(1024) < 0) {
        logger::error("fail to init loop");
        exit(1);
    }
    if (SetupSignalHandler(sh, l) < 0) {
        logger::error("fail to setup signal handler");
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
    AdhocUdpServer us(l, [](AdhocUdpSession &s, const char *p, size_t sz) {
        // echo udp
        logger::info({{"ev","recv packet"}, {"pl", std::string(p, sz)}});
        return s.Send(p, sz);
    });
    if (!us.Listen(9999)) {
        logger::error("fail to listen");
        exit(1);
    }
    while (true) {
        l.Poll();
    }
    return 0;
}

// #define MS_CLASS "mediasoup-worker"
// // #define MS_LOG_DEV_LEVEL 3

// #include "MediaSoupErrors.hpp"
// #include "lib.hpp"
// #include <cstdlib> // std::_Exit()
// #include <string>

// static constexpr int ConsumerChannelFd{ 3 };
// static constexpr int ProducerChannelFd{ 4 };
// static constexpr int PayloadConsumerChannelFd{ 5 };
// static constexpr int PayloadProducerChannelFd{ 6 };

// int main(int argc, char* argv[])
// {
// 	// Ensure we are called by our Node library.
// 	if (!std::getenv("MEDIASOUP_VERSION"))
// 	{
// 		MS_ERROR_STD("you don't seem to be my real father!");

// 		// 41 is a custom exit code to notify about "missing MEDIASOUP_VERSION" env.
// 		std::_Exit(41);
// 	}

// 	const std::string version = std::getenv("MEDIASOUP_VERSION");

// 	auto statusCode = mediasoup_worker_run(
// 	  argc,
// 	  argv,
// 	  version.c_str(),
// 	  ConsumerChannelFd,
// 	  ProducerChannelFd,
// 	  PayloadConsumerChannelFd,
// 	  PayloadProducerChannelFd,
// 	  nullptr,
// 	  nullptr,
// 	  nullptr,
// 	  nullptr,
// 	  nullptr,
// 	  nullptr,
// 	  nullptr,
// 	  nullptr);

// 	std::_Exit(statusCode);
// }
