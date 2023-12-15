#pragma once

#include "RTC/SctpDictionaries.hpp"

#include <functional>

namespace base {
  class Stream;
  class ConnectionFactory {
  public:
    class Connection {
    public:
      typedef struct {
        RTC::SctpStreamParameters params;
        std::string label;
        std::string protocol; // now not used
      } StreamConfig;
    public:
      virtual ~Connection() = default;
    public:
      virtual void Close() = 0;
      virtual int Open(Stream &) = 0;
      virtual void Close(Stream &) = 0;
      virtual int Send(Stream &, const char *, size_t, bool) = 0;
      virtual std::shared_ptr<Stream> OpenStream(const StreamConfig &) = 0;
    };
  public:
    ConnectionFactory() = default;
    virtual ~ConnectionFactory() = default;
  };
  typedef ConnectionFactory::Connection Connection;
  typedef Connection::StreamConfig StreamConfig;
  typedef std::function<std::shared_ptr<Stream> (const StreamConfig &, Connection &)> StreamFactory;
} // namespace base