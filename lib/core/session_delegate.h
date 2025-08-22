#pragma once

#include "base/handler_map.h"
#include "core/serial.h"

namespace qrpc {
class Loop;
class Session;
class Stream;
class SessionDelegate {
 public:
  virtual ~NqSessionDelegate() {}
  virtual void *Context() const = 0;
  virtual bool GetAppProto(const uint8_t **pp_proto, qrpc_size_t *p_proto_len) = 0;
  virtual void Destroy() = 0;
  virtual void OnClose(bool app, qrpc_close_reason_code_t code, 
                       const uint8_t *detail, qrpc_size_t detail_len, bool close_by_peer_or_self) = 0;
  virtual void OnOpen() = 0;
  virtual void Disconnect(qrpc_close_reason_code_t reason, const uint8_t *detail, qrpc_size_t detail_len) = 0;
  virtual bool Reconnect(bool app, qrpc_close_reason_code_t reason, const uint8_t *detail, qrpc_size_t detail_len) = 0; //only supported for client 
  virtual void DoReconnect() = 0;
  virtual void OnReachabilityChange(qrpc_reachability_t state) = 0;
  virtual uint64_t ReconnectDurationUS() const = 0;
  virtual bool IsClient() const = 0;
  virtual bool IsConnected() const = 0;
  virtual Stream *NewStream() = 0;
  virtual void InitStream(const std::string &name, void *ctx) = 0;
  virtual void OpenStream(const std::string &name, void *ctx) = 0;
  virtual void CloseStream(QuicStreamId id) = 0;
  virtual int UnderlyingFd() = 0;
  virtual const HandlerMap *GetHandlerMap() const = 0;
  virtual HandlerMap *ResetHandlerMap() = 0;
  virtual Loop *GetLoop() = 0;
  virtual qrpc_cid_t ConnectionId() = 0;
  virtual void FlushWriteBuffer() = 0;
  virtual const Serial &SessionSerial() const = 0;
  inline SessionIndex SessionIndex() const { 
    return Serial::ObjectIndex<NqSessionIndex>(SessionSerial());
  }
  inline qrpc_conn_t ToHandle() {
    return MakeHandle<qrpc_conn_t, SessionDelegate>(this, SessionSerial());  
  }
  static inline SessionDelegate *FromHandle(qrpc_conn_t c) {
    auto d = reinterpret_cast<NqSessionDelegate *>(c.p);
    return (d != nullptr && d->SessionSerial() == c.s) ? d : nullptr;
  }
};
}