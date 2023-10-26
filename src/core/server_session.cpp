#include "core/nq_server_session.h"

#include "core/nq_server.h"
#include "core/nq_stream.h"
#include "core/nq_dispatcher.h"

namespace qrpc {
ServerSession::NqServerSession(QuicConnection *connection,
                                 const Server::PortConfig &port_config)
  //quic_dispatcher implements QuicSession::Visitor interface                                 
  : ServerSessionCompat(connection, port_config),
  port_config_(port_config), own_handler_map_(), context_(nullptr) {
}
qrpc_conn_t ServerSession::ToHandle() { 
  return MakeHandle<qrpc_conn_t, SessionDelegate>(static_cast<NqSessionDelegate *>(this), session_serial_);
}
std::mutex &NqServerSession::static_mutex() {
  return dispatcher()->session_allocator_body().Bss(this)->mutex();
}
Boxer *NqServerSession::boxer() { 
  return static_cast<NqBoxer *>(dispatcher()); 
}


// stream operation
Stream *NqServerSession::FindStream(QuicStreamId id) {
  return FindStreamImpl(id);
}
Stream *NqServerSession::FindStreamBySerial(const qrpc_serial_t &s, bool include_closed) {
  return FindStreamBySerialImpl(s, include_closed);
}
void ServerSession::InitSerial() {
  auto session_index = dispatcher()->session_idxgen().New();
  ConnSerialCodec::ServerEncode(session_serial_, session_index);
}


//implements SessionDelegate
Loop *NqServerSession::GetLoop() { 
  return dispatcher()->loop(); 
}
void ServerSession::OnClose(bool app, qrpc_close_reason_code_t code, 
                              const uint8_t *msg, qrpc_size_t msglen, bool close_by_peer_or_self) {
  qrpc_close_reason_t detail = {
    .app = app,
    .code = code,
    .msg = reinterpret_cast<const char *>(msg),
    .msglen = msglen
  };
  qrpc_closure_call(port_config_.server().on_close, ToHandle(), 
                  QRPC_EQUIC, 
                  &detail, 
                  close_by_peer_or_self);
  InvalidateSerial(); //now session pointer not valid
}
void ServerSession::OnOpen() {
  qrpc_closure_call(port_config_.server().on_open, ToHandle(), &context_);
}
bool ServerSession::Reconnect(bool, qrpc_close_reason_code_t, const uint8_t *, qrpc_size_t) {
  return false;
}
bool ServerSession::IsClient() const {
  return false;
}
void ServerSession::InitStream(const std::string &name, void *ctx) {
  boxer()->InvokeConn(session_serial_, this, Boxer::OpCode::OpenStream, name.c_str(), ctx);
}
void ServerSession::OpenStream(const std::string &name, void *ctx) {
  auto s = NewStream();
  auto ppctx = s->ContextBuffer();
  *ppctx = ctx;
  if (!s->OpenHandler(name, true)) {
    CloseStream(s->id());
  }
}
int ServerSession::UnderlyingFd() {
  ASSERT(false);
  return -1;
}
const HandlerMap *NqServerSession::GetHandlerMap() const {
  return own_handler_map_ != nullptr ? own_handler_map_.get() : port_config_.handler_map();
}
HandlerMap *NqServerSession::ResetHandlerMap() {
  own_handler_map_.reset(new HandlerMap());
  return own_handler_map_.get();
}
} //net
