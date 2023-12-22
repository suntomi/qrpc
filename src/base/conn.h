#pragma once

#include "base/stream.h"
#include "base/http.h"

#include <functional>

namespace base {
  class Connection {
  public:
    virtual ~Connection() = default;
  public:
    virtual void Close() = 0;
    virtual int Open(Stream &) = 0;
    virtual void Close(Stream &) = 0;
    virtual int Send(Stream &, const char *, size_t, bool) = 0;
    virtual std::shared_ptr<Stream> OpenStream(const Stream::Config &) = 0;
    virtual int OnConnect() = 0;
    virtual void OnShutdown() = 0;
  };
  class Client {
  public:
    virtual ~Client() = default;
  public:
    virtual void Close(Connection &) = 0;
    virtual bool Connect(
      const std::string &host, int port, const std::string &path
    ) = 0;
    inline bool Connect(const std::string &host, int port) {
      return Connect(host, port, "/qrpc");
    }
  };
  class Server {
  public:
    virtual ~Server() = default;
  public:
    // TODO: manage multiple path => port mapping
    virtual void Close(Connection &) = 0;
    virtual bool Listen(
      int signaling_port, int port,
      const std::string &listen_ip, const std::string &path
    ) = 0;
    virtual HttpRouter &RestRouter() = 0;
    inline bool Listen(int sinaling_port) {
      return Listen(sinaling_port, 0, "", "/qrpc");
    }
    inline bool Listen(int sinaling_port, int port) {
      return Listen(sinaling_port, port, "", "/qrpc");
    }
    inline bool Listen(int sinaling_port, int port, const std::string &listen_ip) {
      return Listen(sinaling_port, port, listen_ip, "/qrpc");
    }
  };
} // namespace base