#include "../base/loop.h"
#include "../base/logger.h"
#include "../base/http.h"
#include "../ext/json/include/nlohmann/json.hpp"

using json = nlohmann::json;
using namespace base;

int main(int argc, char *argv[]) {
    Loop l;
    if (l.Open(1024) < 0) {
        TRACE("fail to init loop");
        exit(1);
    }
    HttpServer s;
    HttpRouter r;
    r.Route(R"/.*", [](HttpSession &s) {
        HttpHeader h[] = {
            {.key = "Content-Type", .val = "application/json"},
            {.key = "Content-Length", .val = std::to_string(len)}
        };
        json j = {
            {"sdp", "hoge"}
        };
        std::string body = std::to_string(j);
        s.Write(HRC_OK, &h, 2, body.c_str(), body.length());
    });
    if (!s.Listen(8080, r)) {
        TRACE("fail to listen");
        exit(1);
    }
    if (l.Add(s.fd(), &s, Loop::EV_READ | Loop::EV_WRITE) < 0) {
        TRACE("fail to start server");
        exit(1);
    }
    while (true) {
        l.Poll();
    }
    return 0;
}
