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

class Handler {
    static int count_;
public:
    static void Reset() {
        count_ = 0;
    }
    int Connect(Session &s, std::string proto) {
        logger::info({{"ev","session connected"},{"p",proto},{"a",s.addr().str()},{"c",count_}});
        if (count_ == 0) {
            logger::info({{"ev","kill session on connect"},{"p",proto},{"a",s.addr().str()}});
            count_++;
            return QRPC_EUSER;
        } else {
            return QRPC_OK;
        }
    }
    int Read(Session &s, std::string proto, const char *p, size_t sz) {
        auto pl = std::string(p, sz);
        logger::info({{"ev","session read"},{"p",proto},{"a",s.addr().str()},{"pl", pl}});
        if (pl == "die") {
            logger::info({{"ev","kill session on read"},{"p",proto},{"a",s.addr().str()}});
            return QRPC_EUSER;
        } else if (pl == "timeout") {
            logger::info({{"ev","nothing returns to make peer timedout"},{"p",proto},{"a",s.addr().str()}});
            return QRPC_OK;
        } else {
            return s.Send(p, sz);
        }
    }
    qrpc_time_t Shutdown(Session &s, std::string proto) {
        logger::info({{"ev","session shutdown"},{"p",proto},{"a",s.addr().str()}});
        return 0;
    }
};
int Handler::count_ = 0;
class TestUdpSession : public UdpSession {
    Handler handler_;
public:
    TestUdpSession(UdpSessionFactory &f, Fd fd, const Address &a) : UdpSession(f, fd, a) {}
    int OnConnect() override {
        return handler_.Connect(*this, "udp");
    }
    int OnRead(const char *p, size_t sz) override { return handler_.Read(*this, "udp", p, sz); }
    qrpc_time_t OnShutdown() override { return handler_.Shutdown(*this, "udp"); }
};
class TestTcpSession : public TcpSession {
    Handler handler_;
public:
    TestTcpSession(TcpSessionFactory &f, Fd fd, const Address &a) : TcpSession(f, fd, a) {}
    int OnConnect() override {
        return handler_.Connect(*this, "tcp");
    }
    int OnRead(const char *p, size_t sz) override { return handler_.Read(*this, "tcp", p, sz); }
    qrpc_time_t OnShutdown() override { return handler_.Shutdown(*this, "tcp"); }
};

int main(int argc, char *argv[]) {
    bool alive = true;
    Loop l; {
    if (l.Open(1024) < 0) {
        DIE("fail to init loop");
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
    base::webrtc::AdhocListener w(l, base::webrtc::AdhocListener::Config {
        .max_outgoing_stream_size = 32, .initial_incoming_stream_size = 32,
        .rtp = {
            .initial_outgoing_bitrate = 10000000,
            .max_incoming_bitrate = 10000000,
            .max_outgoing_bitrate = 100000000,
            .min_outgoing_bitrate = 0,
        },
        .send_buffer_size = 256 * 1024,
        .http_timeout = qrpc_time_sec(5),
        .session_timeout = qrpc_time_sec(15), // udp session usally receives stun probing packet statically
        .connection_timeout = qrpc_time_sec(60),
        .consent_check_interval = qrpc_time_sec(10),
        .fingerprint_algorithm = "sha-256",
    }, [](Stream &s, const char *p, size_t sz) {
        auto pl = std::string(p, sz);
        logger::info({{"ev","recv data"},{"l",s.label()},{"sid",s.id()},{"pl", pl}});
        try {
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
        } catch (std::exception &ec_group_str) {
            if (s.label() != "chat") {
                logger::error({{"ev","fail to parse json"},{"l",s.label()},{"sid",s.id()}});
                ASSERT(false);
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
    std::filesystem::path p(__FILE__);
    auto rootpath = p.parent_path().string();
    auto htmlpath = rootpath + "/resources/client.html";
    w.RestRouter().
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
    Route(std::regex("/([^/]*)\\.([^/\?]*)?(.*)"), [&rootpath](HttpSession &s, std::cmatch &m) {
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
            {"ico", "image/x-icon"},
            {"html", "text/html"},
            {"svg", "image/svg+xml"}
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
    Route(std::regex("/reset"), [](HttpSession &s, std::cmatch &) {
        HttpHeader h[] = {
            {.key = "Content-Type", .val = "application/text"},
            {.key = "Content-Length", .val = "5"}
        };
        Handler::Reset();
        s.Respond(HRC_OK, h, 2, "reset", 5);
	    return nullptr;
    }).
    Route(std::regex("/ws"), [](HttpSession &s, std::cmatch &) {
        return WebSocketListener::Upgrade(s, [](WebSocketSession &ws, const char *p, size_t sz) {
            // echo server
            return ws.Send(p, sz);
        });
    });
    if (!w.Listen(8888, 11111)) {
        DIE("fail to listen webrtc");
    }
    AdhocUdpListener us(l, [](AdhocUdpSession &s, const char *p, size_t sz) {
        // echo udp
        logger::info({{"ev","recv packet"},{"a",s.addr().str()},{"pl", std::string(p, sz)}});
        return s.Send(p, sz);
    }, AdhocUdpListener::Config(NopResolver::Instance(), qrpc_time_sec(5), 1, true));
    if (!us.Listen(9999)) {
        DIE("fail to listen on UDP");
    }
    UdpListener tu(l, [&tu](Fd fd, const Address &a) {
        return new TestUdpSession(tu, fd, a);
    }, AdhocUdpListener::Config(NopResolver::Instance(), qrpc_time_sec(5), 1, true));
    if (!tu.Listen(10000)) {
        DIE("fail to listen on UDP for test");
    }
    TcpListener tt(l, [&tt](Fd fd, const Address &a) {
        return new TestTcpSession(tt, fd, a);
    });
    if (!tt.Listen(10001)) {
        DIE("fail to listen on TCP for test");
    }
    while (alive) {
        l.Poll();
    }
    }
    return 0;
}
