#pragma once

#include "base/stream.h"

namespace base {
  class Connection {
  public:
    virtual ~Connection() = default;
  public:
    virtual void Close() = 0;
    virtual int Send(const char *, size_t) = 0;
    virtual int Open(Stream &) = 0;
    virtual void Close(Stream &) = 0;
    virtual int Send(Stream &, const char *, size_t, bool) = 0;
    virtual std::shared_ptr<Stream> OpenStream(const Stream::Config &) = 0;
  };
} // namespace qrpc