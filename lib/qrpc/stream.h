#pragma once

#include "qrpc.h"

#include "qrpc/base.h"

#include "base/alarm.h"
#include "base/stream.h"
#include "base/header_codec.h"
#include "base/stream.h"

namespace base {
  class Connection;
}

namespace qrpc {
  class Stream : public base::Stream {
  public:
    static constexpr size_t LENGTH_BUFFER_SIZE = base::LengthCodec::EncodeLength(sizeof(qrpc_size_t));
    static constexpr size_t HEADER_BUFFER_SIZE = 8;  
  public:
    Stream(base::Connection &c, const Config &config) : base::Stream(c, config), ctx_(nullptr) {}
    ~Stream() override {}
  protected:
    void *ctx_;
  };
  // ByteStream forwards each transport record to the stream handler as-is.
  class ByteStream : public Stream {
  public:
    ByteStream(base::Connection &c, const Config &config, const qrpc_stream_handler_t &h) : Stream(c, config), handler_(h) {}
    ~ByteStream() override {}
    int OnRead(const char *p, size_t sz) override;
    int OnConnect() override { return qrpc_closure_call(handler_.on_stream_open, this->ToHandle(), &ctx_); }
    void OnShutdown() override { qrpc_closure_call(handler_.on_stream_close, this->ToHandle()); }
    qrpc_stream_t ToHandle() { return { .s = this->serial_, .p = this }; }
  private:
    qrpc_stream_handler_t handler_;
  };
  // CodedByteStream restores record boundaries on transports without message framing.
  class CodedByteStream : public Stream {
  public:
    CodedByteStream(base::Connection &c, const Config &config, const qrpc_stream_handler_t &h) : Stream(c, config), handler_(h) {}
    ~CodedByteStream() override {}
    int Send(const char *data, size_t sz) override;
    int OnRead(const char *p, size_t sz) override;
    int OnConnect() override { return qrpc_closure_call(handler_.on_stream_open, this->ToHandle(), &ctx_); }
    void OnShutdown() override { qrpc_closure_call(handler_.on_stream_close, this->ToHandle()); }
    qrpc_stream_t ToHandle() { return { .s = this->serial_, .p = this }; }
  private:
    qrpc_stream_handler_t handler_;
    std::string parse_buffer_;
  };
  // RPCBase provides shared RPC behavior independent from transport framing.
  class RPCBase : public Stream {
    class Request {
    public:
      Request(RPCBase *s, qrpc_msgid_t msgid, qrpc_on_rpc_reply_t on_reply, qrpc_time_t limit_ts) : 
              stream_(*s), on_reply_(on_reply), msgid_(msgid), limit_ts_(limit_ts) {}
      ~Request() {}
      inline void GoAway() {
        qrpc_closure_call(on_reply_, stream_.ToHandle(), QRPC_EGOAWAY, "", 0);
      }
    private:
      friend class RPCBase;
      RPCBase &stream_;
      qrpc_on_rpc_reply_t on_reply_;
      qrpc_msgid_t msgid_;
      qrpc_time_t limit_ts_;
    };
    void EntryRequest(qrpc_msgid_t msgid, qrpc_on_rpc_reply_t cb, qrpc_time_t timeout_duration_ts = 0);    
    bool CompleteRequest(std::unordered_map<qrpc_msgid_t, Request>::iterator it, bool from_timer = false);
  public:
    RPCBase(base::Connection &c, const Config &config, qrpc_rpc_handler_t rpc, base::AlarmProcessor &ap) :
      Stream(c, config), rpc_(rpc), default_timeout_ts_(rpc.timeout), msgid_factory_(), req_map_(), ap_(ap),
      alarm_id_(AlarmProcessor::INVALID_ID) {}
    int OnConnect() override;
    void OnShutdown() override;
    void Notify(uint16_t type, const void *p, qrpc_size_t len);
    void Call(uint16_t type, const void *p, qrpc_size_t len, qrpc_on_rpc_reply_t cb);
    void CallEx(uint16_t type, const void *p, qrpc_size_t len, const qrpc_rpc_opt_t &opt);
    void Reply(qrpc_error_t result, qrpc_msgid_t msgid, const void *p, qrpc_size_t len);
    void Close(const CloseReason &r) override;
  public:
    qrpc_rpc_t ToHandle() { return { .s = this->serial_, .p = this }; }
    static inline RPCBase *FromHandle(qrpc_rpc_t rpc) { return Stream::FromHandle(rpc)->As<RPCBase>(); }
    qrpc_time_t CheckTimeout();
    int ProcessRecord(const char *p, size_t sz);
  protected:
    virtual void SendCommon(int16_t type, qrpc_msgid_t msgid, const void *p, qrpc_size_t len) = 0;
  private:
    qrpc_rpc_handler_t rpc_;
    qrpc_time_t default_timeout_ts_;
    IdFactory<qrpc_msgid_t> msgid_factory_;
    std::unordered_map<qrpc_msgid_t, Request> req_map_;
    base::AlarmProcessor &ap_;
    base::AlarmProcessor::Id alarm_id_;
  };
  // RPCStream expects the transport to preserve message boundaries.
  class RPCStream : public RPCBase {
  public:
    RPCStream(base::Connection &c, const Config &config, qrpc_rpc_handler_t rpc, base::AlarmProcessor &ap) :
      RPCBase(c, config, rpc, ap) {}
    int OnRead(const char *p, size_t sz) override;
  protected:
    void SendCommon(int16_t type, qrpc_msgid_t msgid, const void *p, qrpc_size_t len) override;
  };
  // CodedRPCStream restores RPC record boundaries on byte-stream transports.
  class CodedRPCStream : public RPCBase {
  public:
    CodedRPCStream(base::Connection &c, const Config &config, qrpc_rpc_handler_t rpc, base::AlarmProcessor &ap) :
      RPCBase(c, config, rpc, ap) {}
    int OnRead(const char *p, size_t sz) override;
  protected:
    void SendCommon(int16_t type, qrpc_msgid_t msgid, const void *p, qrpc_size_t len) override;
  private:
    std::string parse_buffer_;
  };

}
