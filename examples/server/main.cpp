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
    })) {
        DIE("fail to setup signal handler");
    }
    webrtc::AdhocServer w(l, webrtc::ConnectionFactory::Config {
        .max_outgoing_stream_size = 32, .initial_incoming_stream_size = 32,
        .send_buffer_size = 256 * 1024,
        .udp_session_timeout = qrpc_time_sec(15), // udp session usally receives stun probing packet statically
        .connection_timeout = qrpc_time_sec(60),
        .fingerprint_algorithm = "sha-256",
        .alarm_processor = t,
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
    // signaling: 8888(http), webrtc: 11111(udp/tcp)
    base::Server &bsv = w;
    std::filesystem::path p(__FILE__);
    auto rootpath = p.parent_path().string();
    auto htmlpath = rootpath + "/resources/client.html";
    bsv.RestRouter().
    Route(std::regex("/"), [&htmlpath](HttpSession &s, std::cmatch &) {
        size_t htmlsz;
        auto html = Syscall::ReadFile(htmlpath, &htmlsz);
        if (html == nullptr) {
            DIE("fail to read html at " + htmlpath);
        }
        auto htmlen = std::to_string(htmlsz);
        HttpHeader h[] = {
            {.key = "Content-Type", .val = "text/html"},
            {.key = "Content-Length", .val = htmlen.c_str()}
        };
        s.Respond(HRC_OK, h, 2, html.get(), htmlsz);
        return nullptr;
    }).
    Route(std::regex("/(.*)\\.(.*)"), [&rootpath](HttpSession &s, std::cmatch &m) {
        size_t filesz;
        auto path = rootpath + "/resources/" + m[1].str() + "." + m[2].str();
        auto file = Syscall::ReadFile(path, &filesz);
        if (file == nullptr) {
            QRPC_LOG(warn, "fail to read html at " + path);
            s.NotFound("fail to read file at " + path);
            return nullptr;
        }
        auto flen = std::to_string(filesz);
        std::map<std::string, std::string> ctypes = {
            {"js", "text/javascript"},
            {"css", "text/css"},
            {"png", "image/png"},
            {"jpg", "image/jpeg"},
            {"jpeg", "image/jpeg"},
            {"gif", "image/gif"},
            {"ico", "image/x-icon"}
        };
        HttpHeader h[] = {
            {.key = "Content-Type", .val = ctypes[m[2].str()].c_str()},
            {.key = "Content-Length", .val = flen.c_str()}
        };
        s.Respond(HRC_OK, h, 2, file.get(), filesz);
        return nullptr;
    }).
    Route(std::regex("/test"), [](HttpSession &s, std::cmatch &) {
        json j = {
            {"sdp", "hoge"}
        };
        std::string body = j.dump();
	    std::string bodylen = std::to_string(body.length());
        HttpHeader h[] = {
            {.key = "Content-Type", .val = "application/json"},
            {.key = "Content-Length", .val = bodylen.c_str()}
        };
        s.Respond(HRC_OK, h, 2, body.c_str(), body.length());
	    return nullptr;
    }).
    Route(std::regex("/ws"), [](HttpSession &s, std::cmatch &) {
        return WebSocketListener::Upgrade(s, [](WebSocketSession &ws, const char *p, size_t sz) {
            // echo server
            return ws.Send(p, sz);
        });
    });
    if (!bsv.Listen(8888, 11111)) {
        DIE("fail to listen webrtc");
    }
    AdhocUdpListener us(l, [](AdhocUdpSession &s, const char *p, size_t sz) {
        // echo udp
        logger::info({{"ev","recv packet"},{"a",s.addr().str()},{"pl", std::string(p, sz)}});
        return s.Send(p, sz);
    }, { .alarm_processor = t, .session_timeout = qrpc_time_sec(5)});
    if (!us.Listen(9999)) {
        DIE("fail to listen on UDP");
    }
    while (alive) {
        l.Poll();
    }
    }}
    return 0;
}
