#pragma once

#include "base/stream.h"

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
    virtual bool has_message_boundary() const = 0;
    virtual int InitMedia(const qrpc_media_config_t &) = 0;
    virtual int OpenMedia(const qrpc_media_produce_config_t &) = 0;
    virtual int CloseMedia(const std::string &) = 0;
    virtual int WatchMedia(const qrpc_media_consume_config_t &) = 0;
    virtual int ControlMedia(const std::string &, const qrpc_media_control_t &) = 0;
    virtual int OnSyscallAck(qrpc_msgid_t msgid, const std::map<std::string,json> &args) = 0;
    virtual bool IsMediaPaused(const std::string &path) = 0;
    virtual AlarmProcessor &alarm_processor() = 0;
    virtual StreamFactory &stream_factory() = 0;
    virtual bool is_client() const = 0;
    virtual void *context() = 0;
  };
} // namespace base
