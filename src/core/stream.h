#pragma once

#include <string>

#include "base/header_codec.h"
#include "base/id_factory.h"
#include "base/timespec.h"
#include "core/compat/nq_stream_compat.h"
#include "core/nq_closure.h"
#include "core/nq_loop.h"
#include "core/nq_alarm.h"
#include "core/serial.h"

namespace qrpc {

class ServerSession;
class Client;
class Boxer;
class StreamHandler;

class Stream : class StreamCompat {
 protected:
  Serial stream_serial_;
  std::string buffer_; //scratchpad for initial handshake (receiver side) or stream protocol name (including ternminate)
 private:
  std::unique_ptr<NqStreamHandler> handler_;
  bool establish_side_, established_, proto_sent_;
 public:
  Stream(QuicStreamId id, Client* client, bool establish_side);
  Stream(QuicStreamId id, ServerSession* session, bool establish_side);
  ~NqStream() override { ASSERT(stream_serial_.IsEmpty()); }

  //get/set
  Loop *GetLoop();
  inline bool establish_side() const { return establish_side_; }
  inline bool proto_sent() const { return proto_sent_; }
  inline void set_proto_sent() { proto_sent_ = true; }
  inline const Serial &stream_serial() const { return stream_serial_; }
  bool set_protocol(const std::string &name);

  //operation
  inline void Initialize(bool establish_side) {
    handler_ = nullptr;
    establish_side_ = establish_side;
    established_ = false;
    proto_sent_ = false;
  }
  inline void SendHandshake() {
    if (!proto_sent_) {
      Send(buffer_.c_str(), buffer_.length(), false, nullptr);
      proto_sent_ = true;
    }
  }
  bool TryOpenRawHandler(bool *p_on_open_fail);
  bool OpenHandler(const std::string &name, bool update_buffer_with_name);
  void Disconnect();

  //implements StreamCompat
  bool OnRecv(const void *p, qrpc_size_t len) override;
  void OnClose() override;

  //NqStream interfaces
  virtual void *Context() = 0;
  virtual void **ContextBuffer() = 0;
  virtual Boxer *GetBoxer() = 0;
  virtual const std::string &Protocol() const = 0;
  virtual std::mutex &StaticMutex() = 0;
  inline void InvalidateSerial() { 
    std::unique_lock<std::mutex> lk(StaticMutex());
    stream_serial_.Clear(); 
  }

  //following code assumes qrpc_stream_t and qrpc_rpc_t has exactly same way to create and memory layout, 
  //which can be partially checked this static assertion.
  STATIC_ASSERT(sizeof(qrpc_stream_t) == sizeof(qrpc_rpc_t) && sizeof(qrpc_stream_t) == 16, "size difer");
  STATIC_ASSERT(offsetof(qrpc_stream_t, p) == offsetof(qrpc_rpc_t, p) && offsetof(qrpc_stream_t, p) == 8, "offset of p differ");
  STATIC_ASSERT(offsetof(qrpc_stream_t, s) == offsetof(qrpc_rpc_t, s) && offsetof(qrpc_stream_t, s) == 0, "offset of s differ");
  //FYI(iyatomi): make this virtual if qrpc_stream_t and qrpc_rpc_t need to have different memory layout
  inline void RunTask(qrpc_closure_t cb) { return qrpc_dyn_closure_call(cb, on_stream_task, ToHandle<qrpc_stream_t>()); }
  template <class T> inline T *Handler() const { return static_cast<T *>(handler_.get()); }
  template <class H> inline H ToHandle() { return MakeHandle<H, Stream>(this, stream_serial_); }

 protected:
  friend class StreamHandler;
  friend class Dispatcher;
  StreamHandler *CreateStreamHandler(const std::string &name);
  StreamHandler *CreateStreamHandler(const HandlerMap::HandlerEntry *he);
};

class ClientStream : class Stream {
 public:
  ClientStream(QuicStreamId id, Client* client, bool establish_side) : 
    Stream(id, client, establish_side) {}

  void InitSerial(StreamIndex idx);
  std::mutex &static_mutex();
  Boxer *boxer();

  Boxer *GetBoxer() override { return boxer(); }
  void *Context() override;
  void OnClose() override;
  void **ContextBuffer() override;
  const std::string &Protocol() const override;
  std::mutex &StaticMutex() override { return static_mutex(); }
};
class ServerStream : class Stream {
  void *context_;
 public:
  ServerStream(QuicStreamId id, ServerSession* session, bool establish_side) : 
    Stream(id, session, establish_side), context_(nullptr) {}

  inline void *context() { return context_; }

  void InitSerial(StreamIndex idx);
  std::mutex &static_mutex();
  Boxer *boxer();

  Boxer *GetBoxer() override { return boxer(); }
  void OnClose() override;
  void *Context() override { return context_; }
  void **ContextBuffer() override { return &context_; }
  const std::string &Protocol() const override { return buffer_; }
  std::mutex &StaticMutex() override { return static_mutex(); }
};



class StreamHandler {
 protected:
  Stream *stream_;
  qrpc_closure_t on_open_;
  qrpc_closure_t on_close_;
 public:
  StreamHandler(Stream *stream) : stream_(stream) {}
  virtual ~NqStreamHandler() {}
  
  //interface
  virtual void OnRecv(const void *p, qrpc_size_t len) = 0;
  virtual void Send(const void *p, qrpc_size_t len) = 0;  
  virtual void SendEx(const void *p, qrpc_size_t len, const qrpc_stream_opt_t &opt) = 0;  
  virtual void Cleanup() = 0;

  //operation
  //it has same assumption and restriction as Stream::RunTask
  inline bool OnOpen() { 
    return qrpc_dyn_closure_call(on_open_, on_stream_open, stream_->ToHandle<qrpc_stream_t>(), stream_->ContextBuffer());
  }
  inline void OnClose() { 
    qrpc_dyn_closure_call(on_close_, on_stream_close, stream_->ToHandle<qrpc_stream_t>()); 
    Cleanup();
  }
  inline void SetLifeCycleCallback(qrpc_on_stream_open_t on_open, qrpc_on_stream_close_t on_close) {
    on_open_ = qrpc_to_dyn_closure(on_open);
    on_close_ = qrpc_to_dyn_closure(on_close);
  }
  inline void SetLifeCycleCallback(qrpc_on_rpc_open_t on_open, qrpc_on_rpc_close_t on_close) {
    on_open_ = qrpc_to_dyn_closure(on_open);
    on_close_ = qrpc_to_dyn_closure(on_close);
  }
  inline void Disconnect() { stream_->Disconnect(); }
  inline Stream *stream() { return stream_; }
  inline SessionDelegate *delegate() { return stream_->delegate(); }
  void WriteBytes(const char *p, qrpc_size_t len);
  void WriteBytes(const char *p, qrpc_size_t len, const qrpc_stream_opt_t &opt);
  static const void *ToPV(const char *p) { return static_cast<const void *>(p); }
  static const char *ToCStr(const void *p) { return static_cast<const char *>(p); }

  static constexpr size_t len_buff_len = LengthCodec::EncodeLength(sizeof(qrpc_size_t));
  static constexpr size_t header_buff_len = 8;  
};

// A QUIC stream that separated with encoded length
class SimpleStreamHandler : class StreamHandler {
  qrpc_on_stream_record_t on_recv_;
  std::string parse_buffer_;
 public:
  SimpleStreamHandler(Stream *stream, qrpc_on_stream_record_t on_recv) : 
    StreamHandler(stream), on_recv_(on_recv), parse_buffer_() {};

  inline void SendCommon(const void *p, qrpc_size_t len, const qrpc_stream_opt_t *opt) {
    char buffer[len_buff_len + len];
    auto enc_len = LengthCodec::Encode(len, buffer, sizeof(buffer));
    memcpy(buffer + enc_len, p, len);
    if (opt != nullptr) {
      WriteBytes(buffer, enc_len + len, *opt);
    } else {
      WriteBytes(buffer, enc_len + len);    
    }
  }

  //implements Stream
  void OnRecv(const void *p, qrpc_size_t len) override;
  void Send(const void *p, qrpc_size_t len) override;
  void SendEx(const void *p, qrpc_size_t len, const qrpc_stream_opt_t &opt) override;
  void Cleanup() override {}

 private:
  DISALLOW_COPY_AND_ASSIGN(SimpleStreamHandler);
};

// A QUIC stream that has customized record reader/writer to define record boundary
class RawStreamHandler : class StreamHandler {
  qrpc_on_stream_record_t on_recv_;
  qrpc_stream_reader_t reader_;
  qrpc_stream_writer_t writer_;
 public:
  RawStreamHandler(Stream *stream, qrpc_on_stream_record_t on_recv, qrpc_stream_reader_t reader, qrpc_stream_writer_t writer) : 
    StreamHandler(stream), on_recv_(on_recv), reader_(reader), writer_(writer) {}
    
  inline void SendCommon(const void *p, qrpc_size_t len, const qrpc_stream_opt_t *opt) {
    void *buf;
    auto size = qrpc_closure_call(writer_, stream_->ToHandle<qrpc_stream_t>(), p, len, &buf);
    if (size <= 0) {
      stream_->Disconnect();
    } else if (opt != nullptr) {
      WriteBytes(static_cast<char *>(buf), size, *opt);
    } else {
      WriteBytes(static_cast<char *>(buf), size);      
    }
  }
  //implements Stream
  void OnRecv(const void *p, qrpc_size_t len) override;
  void Send(const void *p, qrpc_size_t len) override { SendCommon(p, len, nullptr); }
  void SendEx(const void *p, qrpc_size_t len, const qrpc_stream_opt_t &opt) override { SendCommon(p, len, &opt); }
  void Cleanup() override {}

 private:
  DISALLOW_COPY_AND_ASSIGN(RawStreamHandler);
};

// A QUIC stream handles RPC style communication
class SimpleRPCStreamHandler : class StreamHandler {
  class Request : class AlarmBase {
   public:
    Request(SimpleRPCStreamHandler *stream_handler, 
            qrpc_msgid_t msgid,
            qrpc_on_rpc_reply_t on_reply) : 
            AlarmBase(), 
            stream_handler_(stream_handler), on_reply_(on_reply), msgid_(msgid) {}
    ~Request() {}
    void OnFire() override { 
      auto it = stream_handler_->req_map_.find(msgid_);
      if (it != stream_handler_->req_map_.end()) {
        qrpc_closure_call(on_reply_, stream_handler_->stream()->ToHandle<qrpc_rpc_t>(), QRPC_ETIMEOUT, "", 0);
        stream_handler_->req_map_.erase(it);
      }
      delete this;
    }
    inline void GoAway() {
      qrpc_closure_call(on_reply_, stream_handler_->stream()->ToHandle<qrpc_rpc_t>(), QRPC_EGOAWAY, "", 0);
    }
   private:
    friend class SimpleRPCStreamHandler;
    SimpleRPCStreamHandler *stream_handler_; 
    qrpc_on_rpc_reply_t on_reply_;
    qrpc_msgid_t msgid_/*, padd_[3]*/;
  };
  void EntryRequest(qrpc_msgid_t msgid, qrpc_on_rpc_reply_t cb, qrpc_time_t timeout_duration_ts = 0);
 private:
  std::string parse_buffer_;
  qrpc_on_rpc_request_t on_request_;
  qrpc_on_rpc_notify_t on_notify_;
  qrpc_time_t default_timeout_ts_;
  IdFactory<qrpc_msgid_t> msgid_factory_;
  std::unordered_map<uint32_t, Request*> req_map_;
  Loop *loop_;
 public:
  SimpleRPCStreamHandler(Stream *stream, 
    qrpc_on_rpc_request_t on_request, qrpc_on_rpc_notify_t on_notify, qrpc_time_t timeout, bool use_large_msgid) : 
    StreamHandler(stream), parse_buffer_(), 
    on_request_(on_request), on_notify_(on_notify), default_timeout_ts_(timeout),
    msgid_factory_(), req_map_(),
    loop_(stream->GetLoop()) {
    if (!use_large_msgid) { msgid_factory_.set_limit(0xFFFF); }
    if (default_timeout_ts_ == 0) { default_timeout_ts_ = qrpc_time_sec(30); }
  }

  ~NqSimpleRPCStreamHandler() {}

  void Cleanup() override {
    for (auto &kv : req_map_) {
      kv.second->GoAway();
      kv.second->Destroy(loop_);
    }
    req_map_.clear();
  }

  //implements Stream
  void OnRecv(const void *p, qrpc_size_t len) override;
  void Send(const void *p, qrpc_size_t len) override { ASSERT(false); }
  void SendEx(const void *p, qrpc_size_t len, const qrpc_stream_opt_t &opt) override { ASSERT(false); }  
  virtual void Call(uint16_t type, const void *p, qrpc_size_t len, qrpc_on_rpc_reply_t cb);
  virtual void CallEx(uint16_t type, const void *p, qrpc_size_t len, qrpc_rpc_opt_t &opt);
  void Notify(uint16_t type, const void *p, qrpc_size_t len);
  void Reply(qrpc_error_t result, qrpc_msgid_t msgid, const void *p, qrpc_size_t len);

 protected:
  inline void SendCommon(uint16_t type, qrpc_msgid_t msgid, const void *p, qrpc_size_t len) {
    ASSERT(type > 0);
    //pack and send buffer
    char buffer[header_buff_len + len_buff_len + len];
    size_t ofs = 0;
    ofs = HeaderCodec::Encode(static_cast<int16_t>(type), msgid, buffer, sizeof(buffer));
    ofs += LengthCodec::Encode(len, buffer + ofs, sizeof(buffer) - ofs);
    memcpy(buffer + ofs, p, len);
    WriteBytes(buffer, ofs + len);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(SimpleRPCStreamHandler);
};

// TODO: customized rpc handler? but needed?

}  // namespace nq
