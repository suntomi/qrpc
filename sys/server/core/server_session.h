#pragma once

#include <map>
#include <mutex>

#include "core/compat/nq_server_session_compat.h"
#include "core/nq_session_delegate.h"
#include "core/nq_server.h"
#include "core/nq_config.h"

namespace qrpc {
class Stream;
class ServerSession : public SessionDelegate {
 public:
  ServerSession(QuicConnection *connection,
                  const Server::PortConfig &port_config);        
  ~NqServerSession() override { ASSERT(session_serial_.IsEmpty()); }

  qrpc_conn_t ToHandle();
  Stream *FindStream(QuicStreamId id);
  //if you set included closed to true, be careful to use returned value, 
  //this pointer soon will be invalid.
  Stream *FindStreamBySerial(const qrpc_serial_t &s, bool include_closed = false);
  void InitSerial();
  inline void InvalidateSerial() { 
    std::unique_lock<std::mutex> lk(static_mutex());
    session_serial_.Clear(); 
  }

  std::mutex &static_mutex();
  Boxer *boxer();
  inline const Serial &session_serial() const { return session_serial_; }
  inline SessionIndex session_index() const { 
    return ConnSerialCodec::ServerSessionIndex(session_serial_); }

  //implements SessionDelegate
  void *Context() const override { return context_; }
  void Destroy() override { ASSERT(false); }
  void DoReconnect() override { ASSERT(false); }
  void OnReachabilityChange(qrpc_reachability_t) override { ASSERT(false); }
  void OnClose(bool app, qrpc_close_reason_code_t error, 
               const uint8_t *detail, qrpc_size_t detail_len, bool close_by_peer_or_self) override;
  void OnOpen() override;
  void Disconnect(qrpc_close_reason_code_t reason, const uint8_t *detail, qrpc_size_t detail_len) override { 
    DisconnectImpl(reason, detail, detail_len); 
  }
  bool Reconnect(bool, qrpc_close_reason_code_t, const uint8_t *, qrpc_size_t) override;
  bool IsClient() const override;
  bool IsConnected() const override { return true; }
  Stream *NewStream() override;
  void InitStream(const std::string &name, void *ctx) override;
  void OpenStream(const std::string &name, void *ctx) override;
  void CloseStream(QuicStreamId id) override { CloseStreamImpl(id); }
  int UnderlyingFd() override;
  const HandlerMap *GetHandlerMap() const override;
  HandlerMap *ResetHandlerMap() override;
  Loop *GetLoop() override;
  uint64_t ReconnectDurationUS() const override { return 0; }
  qrpc_cid_t ConnectionId() override { return ConnectionIdImpl(); }
  void FlushWriteBuffer() override { FlushWriteBufferImpl(); }
  const Serial &SessionSerial() const override { return session_serial(); }

 private:
  const Server::PortConfig &port_config_;
  std::unique_ptr<HandlerMap> own_handler_map_;
  Serial session_serial_;
  void *context_;
  
};

}
