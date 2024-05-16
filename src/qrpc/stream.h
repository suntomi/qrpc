#pragma once

#include "qrpc.h"

#include "qrpc/base.h"
#include "qrpc/handler_map.h"

#include "base/alarm.h"
#include "base/conn.h"
#include "base/header_codec.h"
#include "base/stream.h"

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
  // CodedByteStream is a stream that has simple coded record length in the beginning of each record.
  class CodedByteStream : public Stream {
  public:
    CodedByteStream(base::Connection &c, const Config &config, const qrpc_stream_handler_t &h) : Stream(c, config), handler_(h) {}
    ~CodedByteStream() override {}
    int OnRead(const char *p, size_t sz) override;
    int OnConnect() override { return qrpc_closure_call(handler_.on_stream_open, this->ToHandle(), &ctx_); }
    void OnShutdown() override { qrpc_closure_call(handler_.on_stream_close, this->ToHandle()); }
    qrpc_stream_t ToHandle() { return { .s = 0, .p = this }; }
  private:
    qrpc_stream_handler_t handler_;
    std::string parse_buffer_;
  };
  // RawByteStream is a stream that user has to define record boundary by providing stream reader/writer
  class RawByteStream : public Stream {
  public:
    RawByteStream(base::Connection &c, const Config &config, const qrpc_stream_handler_t &h) : Stream(c, config), handler_(h) {}
    ~RawByteStream() override {}
    int OnRead(const char *p, size_t sz) override;
    int OnConnect() override { return qrpc_closure_call(handler_.on_stream_open, this->ToHandle(), &ctx_); }
    void OnShutdown() override { return qrpc_closure_call(handler_.on_stream_close, this->ToHandle()); }
    qrpc_stream_t ToHandle() { return { .s = 0, .p = this }; }
  private:
    qrpc_stream_handler_t handler_;
  };
  // RPC Stream is a stream that provides bidirectional asynchronous RPC call
  class RPCStream : public Stream {
    class Request {
    public:
      Request(RPCStream &s, qrpc_msgid_t msgid, qrpc_on_rpc_reply_t on_reply, qrpc_time_t limit_ts) : 
              stream_(s), on_reply_(on_reply), msgid_(msgid), limit_ts_(limit_ts) {}
      ~Request() {}
      inline void GoAway() {
        qrpc_closure_call(on_reply_, stream_.ToHandle(), QRPC_EGOAWAY, "", 0);
      }
    private:
      friend class RPCStream;
      RPCStream &stream_;
      qrpc_on_rpc_reply_t on_reply_;
      qrpc_msgid_t msgid_;
      qrpc_time_t limit_ts_;
    };
    void EntryRequest(qrpc_msgid_t msgid, qrpc_on_rpc_reply_t cb, qrpc_time_t timeout_duration_ts = 0);    
    bool CompleteRequest(std::unordered_map<qrpc_msgid_t, Request>::iterator it, bool from_timer = false);
  public:
    RPCStream(base::Connection &c, const Config &config, qrpc_rpc_handler_t rpc, base::AlarmProcessor &ap) :
      Stream(c, config), rpc_(rpc), default_timeout_ts_(rpc.timeout), msgid_factory_(), req_map_(), ap_(ap),
      alarm_id_(AlarmProcessor::INVALID_ID) {}
    int OnRead(const char *p, size_t sz) override;
    int OnConnect() override;
    void OnShutdown() override;
    void Notify(uint16_t type, const void *p, qrpc_size_t len);
    void Call(uint16_t type, const void *p, qrpc_size_t len, qrpc_on_rpc_reply_t cb);
    void CallEx(uint16_t type, const void *p, qrpc_size_t len, qrpc_rpc_opt_t &opt);
    void Reply(qrpc_error_t result, qrpc_msgid_t msgid, const void *p, qrpc_size_t len);
    void Close(const CloseReason &r) override;
  public:
    qrpc_rpc_t ToHandle() { return { .s = 0, .p = this }; }
    qrpc_time_t CheckTimeout();
    inline void SendCommon(uint16_t type, qrpc_msgid_t msgid, const void *p, qrpc_size_t len) {
      ASSERT(type > 0);
      //pack and send buffer
      char buffer[HEADER_BUFFER_SIZE + LENGTH_BUFFER_SIZE + len];
      size_t ofs = 0;
      ofs = base::HeaderCodec::Encode(static_cast<int16_t>(type), msgid, buffer, sizeof(buffer));
      ofs += base::LengthCodec::Encode(len, buffer + ofs, sizeof(buffer) - ofs);
      memcpy(buffer + ofs, p, len);
      Send(buffer, ofs + len);
    }

  private:
    qrpc_rpc_handler_t rpc_;
    qrpc_time_t default_timeout_ts_;
    IdFactory<qrpc_msgid_t> msgid_factory_;
    std::unordered_map<qrpc_msgid_t, Request> req_map_;
    std::string parse_buffer_;
    base::AlarmProcessor &ap_;
    base::AlarmProcessor::Id alarm_id_;
  };

}