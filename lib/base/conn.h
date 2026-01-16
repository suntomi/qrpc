#pragma once

#include "base/stream.h"
#include "base/serial.h"

namespace base {
  class Connection {
  public:
    virtual ~Connection() = default;
  public:
    virtual void Close() = 0;
    virtual void Reset() = 0;
    virtual int Send(const char *, size_t) = 0;
    virtual int Open(Stream &) = 0;
    virtual void Close(Stream &) = 0;
    virtual int Send(Stream &, const char *, size_t, bool) = 0;
    virtual std::shared_ptr<Stream> OpenStream(const Stream::Config &) = 0;
    virtual AlarmProcessor &alarm_processor() = 0;
    virtual StreamFactory &stream_factory() = 0;
    virtual const Serial &serial() const = 0;
    virtual bool is_client() const = 0;
    virtual void *context() = 0;
    inline qrpc_conn_t ToHandle() const {
      return { .p = this, .s = this->serial() };
    }
    static inline Connection *FromHandle(qrpc_conn_t conn) {
      auto p = reinterpret_cast<const Connection *>(conn.p);
      if (p->serial() != Serial(&conn.s)) {
        return nullptr;
      }
      return const_cast<Connection *>(p);
    }
  };
} // namespace base