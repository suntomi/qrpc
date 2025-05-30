#include "core/nq_stream.h"

#include "base/endian.h"
#include "core/nq_loop.h"
#include "core/nq_client.h"
#include "core/nq_server_session.h"
#include "core/nq_client_loop.h"
#include "core/nq_dispatcher.h"
#include "core/nq_unwrapper.h"

namespace qrpc {

Stream::NqStream(QuicStreamId id, Client* client, bool establish_side) : 
  StreamCompat(id, client) {
  Initialize(establish_side);
}
Stream::NqStream(QuicStreamId id, ServerSession* session, bool establish_side) : 
  StreamCompat(id, session) {
  Initialize(establish_side);
}
bool Stream::TryOpenRawHandler(bool *p_on_open_result) {
  auto rh = delegate()->GetHandlerMap()->RawHandler();
  if (rh != nullptr) {
    handler_ = std::unique_ptr<NqStreamHandler>(CreateStreamHandler(rh));
    proto_sent_ = true;
    established_ = true;
    *p_on_open_result = handler_->OnOpen();
    TRACE("TryOpenRawHandler: established!");
    return true;
  }
  return false;
}
bool Stream::OpenHandler(const std::string &name, bool update_buffer_with_name) {
  if (handler_ != nullptr) {
    return establish_side(); //because non establish side never call this twice.
  }
  bool on_open_result;
  if (TryOpenRawHandler(&on_open_result)) {
    //should open raw handler but OnOpen fails. 
    //caller behave same as usual handler_->OnOpen failure
    return on_open_result;
  }
  //FYI(iyatomi): name.c_str() will create new string without null terminate
  handler_ = std::unique_ptr<NqStreamHandler>(CreateStreamHandler(name.c_str()));
  if (handler_ == nullptr) {
    ASSERT(false);
    return false;
  }
  if (update_buffer_with_name) {
    buffer_ = name;
    buffer_.append(1, '\0');
  }
  if (!handler_->OnOpen()) {
    buffer_ = "";
    return false;
  }
  return true;
}
Loop *NqStream::GetLoop() { 
  return delegate()->GetLoop(); 
}
StreamHandler *NqStream::CreateStreamHandler(const std::string &name) {
  auto he = delegate()->GetHandlerMap()->Find(name);
  if (he == nullptr) {
    ASSERT(false);
    return nullptr;
  }
  return CreateStreamHandler(he);
}
StreamHandler *NqStream::CreateStreamHandler(const HandlerMap::HandlerEntry *he) {
  StreamHandler *s;
  switch (he->type) {
  case HandlerMap::FACTORY: {
    s = (StreamHandler *)nq_closure_call(he->factory, delegate()->ToHandle());
  } break;
  case HandlerMap::STREAM: {
    if (nq_closure_is_empty(he->stream.stream_reader)) {
      s = new SimpleStreamHandler(this, he->stream.on_stream_record);
    } else {
      s = new RawStreamHandler(this, he->stream.on_stream_record, 
                                 he->stream.stream_reader, 
                                 he->stream.stream_writer); 
    }
    s->SetLifeCycleCallback(he->stream.on_stream_open, he->stream.on_stream_close);
  } break;
  case HandlerMap::RPC: {
    s = new SimpleRPCStreamHandler(this, he->rpc.on_rpc_request, 
                                     he->rpc.on_rpc_notify, 
                                    he->rpc.timeout,
                                    he->rpc.use_large_msgid);
    s->SetLifeCycleCallback(he->rpc.on_rpc_open, he->rpc.on_rpc_close);
  } break;
  default:
    ASSERT(false);
    return nullptr;
  }
  return s;
}
void Stream::Disconnect() {
  delegate()->CloseStream(id());
}
void Stream::OnClose() {
  TRACE("NqStream::OnClose %p, %p, %s(%lu)", this, delegate(), stream_serial_.Dump().c_str(), id());
  if (handler_ != nullptr) {
    handler_->OnClose();
  }
  StreamCompat::OnClose();
}
bool Stream::OnRecv(const void *p, qrpc_size_t len) {
  bool on_raw_open_result;
  if (!established_) {
    //establishment
    if (establish_side()) {
      established_ = true;
      //all received packet for establish side processes immediately
    } else if (TryOpenRawHandler(&on_raw_open_result)) {
      if (!on_raw_open_result) {
        Disconnect();
        return true;
      }
      set_proto_sent();
      // established_ = true; TryOpenRawHandler set established_ to true
      //raw handler processes packet immediately without creating stream handler
    } else {
      const char *vbuf = StreamHandler::ToCStr(p);
      size_t idx = 0;
      for (;idx < len; idx++) {
        if (vbuf[idx] == 0) {
          //FYI(iyatomi): this adds null terminate also.
          buffer_.append(vbuf, idx + 1); 
          break;
        }
      }
      if (idx >= len) {
        //FYI(iyatomi): entire buffer points part of string.
        buffer_.append(StreamHandler::ToCStr(p), len);
        return false;
      }
      //prevent send handshake message to client
      set_proto_sent();
      //create handler by initial establish string
      if (!OpenHandler(buffer_, false)) {
        Disconnect();
        return true;
      }
      if (len > (idx + 1)) {
        //v[i] may contains over-received payload
        handler_->OnRecv(vbuf + idx + 1, len - idx - 1);
      }
      established_ = true;
      return false; //p, len is consumed completely for establishment
    }
  }
  //normal receiving packet
  handler_->OnRecv(StreamHandler::ToCStr(p), len);
  return false;
}



Boxer *NqClientStream::boxer() {
  return static_cast<NqBoxer *>(client_loop());
}
void ClientStream::InitSerial(StreamIndex idx) { 
  auto session_serial = delegate()->SessionSerial();
  ASSERT(Serial::IsClient(session_serial));
  StreamSerialCodec::ClientEncode(stream_serial_, idx); 
  TRACE("NqClientStream: serial = %s", stream_serial_.Dump().c_str());
}
void ClientStream::OnClose() {
  ASSERT(delegate()->IsClient());
  Stream::OnClose();
  //remove stream entry after handler_->OnClose called. otherwise callback cannot get context_
  auto c = static_cast<NqClient *>(delegate());
  c->stream_manager().OnClose(this);
  InvalidateSerial();
}
void **NqClientStream::ContextBuffer() {
  ASSERT(delegate()->IsClient());
  auto c = static_cast<NqClient *>(delegate());  
  auto serial = stream_serial();
  return c->stream_manager().FindContextBuffer(
    StreamSerialCodec::ClientStreamIndex(serial));
}
void *NqClientStream::Context() {
  ASSERT(delegate()->IsClient());
  auto c = static_cast<NqClient *>(delegate());  
  auto serial = stream_serial();
  return c->stream_manager().FindContext(
    StreamSerialCodec::ClientStreamIndex(serial));
}
const std::string &NqClientStream::Protocol() const {
  auto c = static_cast<const Client *>(delegate());  
  return c->stream_manager().FindStreamName(
    StreamSerialCodec::ClientStreamIndex(stream_serial()));
}
std::mutex &NqClientStream::static_mutex() {
  auto cl = client_loop(); 
  return cl->stream_allocator().Bss(this)->mutex();
}



Boxer *NqServerStream::boxer() {
  return static_cast<NqBoxer *>(dispatcher());
}
void ServerStream::InitSerial(StreamIndex idx) {
  auto session_serial = delegate()->SessionSerial();
  ASSERT(!NqSerial::IsClient(session_serial));
  StreamSerialCodec::ServerEncode(stream_serial_, idx);
}
void ServerStream::OnClose() {
  ASSERT(!delegate()->IsClient());
  Stream::OnClose();
  InvalidateSerial();
}
std::mutex &NqServerStream::static_mutex() {
  auto dp = dispatcher();  
  return dp->stream_allocator().Bss(this)->mutex();
}



void StreamHandler::WriteBytes(const char *p, qrpc_size_t len) {
  stream_->SendHandshake();
  stream_->Send(p, len, false, nullptr);
}
void StreamHandler::WriteBytes(const char *p, qrpc_size_t len, const qrpc_stream_opt_t &opt) {
  stream_->SendHandshake();
  //TODO(iyatomi): do we need common ack_callback, which is applied to all stream bytes sent?
  stream_->Send(p, len, false, &opt);
}



void SimpleStreamHandler::OnRecv(const void *p, qrpc_size_t len) {
  //greedy read and called back
	parse_buffer_.append(ToCStr(p), len);
	const char *pstr = parse_buffer_.c_str();
	size_t plen = parse_buffer_.length();
	qrpc_size_t reclen = 0, read_ofs = LengthCodec::Decode(&reclen, pstr, plen);
	if (reclen > 0 && (reclen + read_ofs) <= plen) {
	  qrpc_closure_call(on_recv_, stream_->ToHandle<qrpc_stream_t>(), pstr + read_ofs, reclen);
	  parse_buffer_.erase(0, reclen + read_ofs);
	} else if (reclen == 0 && plen > len_buff_len) { //TODO(iyatomi): use unlikely
		//broken payload. should resolve payload length
		stream_->Disconnect();
	}
}
void SimpleStreamHandler::Send(const void *p, qrpc_size_t len) {
  DefferedFlushStream();
  SendCommon(p, len, nullptr);
}
void SimpleStreamHandler::SendEx(const void *p, qrpc_size_t len, const qrpc_stream_opt_t &opt) {
  DefferedFlushStream();
  SendCommon(p, len, &opt);
}



void RawStreamHandler::OnRecv(const void *p, qrpc_size_t len) {
  int reclen;
  void *rec = qrpc_closure_call(reader_, stream_->ToHandle<qrpc_stream_t>(), ToCStr(p), len, &reclen);
  if (rec != nullptr) {
    qrpc_closure_call(on_recv_, stream_->ToHandle<qrpc_stream_t>(), rec, reclen);
  } else if (reclen < 0) {
    stream_->Disconnect();    
  }
}
  


void SimpleRPCStreamHandler::EntryRequest(qrpc_msgid_t msgid, qrpc_on_rpc_reply_t cb, qrpc_time_t timeout_duration_ts) {
  if (stream()->stream_serial().IsEmpty()) {
    //if StreamHandler::WriteBytes fails, stream closed before returning it. 
    return;
  }
  auto req = new Request(this, msgid, cb);
  req_map_[msgid] = req;
  auto now = qrpc_time_now();
  req->Start(loop_, now + timeout_duration_ts);
}
void SimpleRPCStreamHandler::OnRecv(const void *p, qrpc_size_t len) {
  //TRACE("stream %llx handler OnRecv %u bytes", stream_->delegate()->SessionSerial().data[0], len);
  //greedy read and called back
  parse_buffer_.append(ToCStr(p), len);
  //prepare tmp variables
  const char *pstr = parse_buffer_.c_str();
  size_t plen = parse_buffer_.length(), read_ofs;
  int16_t type_tmp; qrpc_msgid_t msgid; qrpc_size_t reclen;
  qrpc_error_t type;
  do {
    //decode header
    read_ofs = HeaderCodec::Decode(&type_tmp, &msgid, pstr, plen);
    /* tmp_ofs => length of encoded header, reclen => actual payload length */
    auto tmp_ofs = LengthCodec::Decode(&reclen, pstr + read_ofs, plen - read_ofs);
    if (tmp_ofs == 0) { break; }
    read_ofs += tmp_ofs;
    if ((read_ofs + reclen) > plen) {
      //TRACE("short of buffer %u %zu %zu\n", reclen, read_ofs, plen);
      break;
    }
    //TRACE("sid = %llx, msgid, type = %u %d", stream_->delegate()->SessionSerial(), msgid, type_tmp);
    /*
      type > 0 && msgid != 0 => request
      type <= 0 && msgid != 0 => reply
      type > 0 && msgid == 0 => notify
    */
    type = static_cast<qrpc_error_t>(type_tmp);
    pstr += read_ofs; //move pointer to top of payload
    if (msgid != 0) {
      if (type <= 0) {
        auto it = req_map_.find(msgid);
        if (it != req_map_.end()) {
          auto req = it->second;
          req_map_.erase(it);
          //reply from serve side
          qrpc_closure_call(req->on_reply_, stream_->ToHandle<qrpc_rpc_t>(), type, ToPV(pstr), reclen);
          req->Destroy(loop_); //cancel firing alarm and free memory for req
        } else {
          //probably timedout. caller should already be received timeout error
          //req object deleted in OnAlarm
          //TRACE("stream handler reply: msgid not found %u", msgid);
        }
      } else {
        //request
        //fprintf(stderr, "stream handler request: idx %u %llu\n", 
          //Endian::NetbytesToHost<uint32_t>(pstr), 
          //Endian::NetbytesToHost<uint64_t>(pstr + 4));
        qrpc_closure_call(on_request_, stream_->ToHandle<qrpc_rpc_t>(), type, msgid, ToPV(pstr), reclen);
      }
    } else if (type > 0) {
      //notify
      //TRACE("stream handler notify: type %u", type);
      qrpc_closure_call(on_notify_, stream_->ToHandle<qrpc_rpc_t>(), type, ToPV(pstr), reclen);
    } else {
      ASSERT(false);
    }
    parse_buffer_.erase(0, reclen + read_ofs);
    pstr = parse_buffer_.c_str();
    plen = parse_buffer_.length();
  } while (parse_buffer_.length() > 0);
}
void SimpleRPCStreamHandler::Notify(uint16_t type, const void *p, qrpc_size_t len) {
  DefferedFlushStream();
  ASSERT(type > 0);
  //pack and send buffer
  char buffer[header_buff_len + len_buff_len + len];
  size_t ofs = 0;
  ofs = HeaderCodec::Encode(static_cast<int16_t>(type), 0, buffer, sizeof(buffer));
  ofs += LengthCodec::Encode(len, buffer + ofs, sizeof(buffer) - ofs);
  memcpy(buffer + ofs, p, len);
  WriteBytes(buffer, ofs + len);  
}
void SimpleRPCStreamHandler::Call(uint16_t type, const void *p, qrpc_size_t len, qrpc_on_rpc_reply_t cb) {
  qrpc_msgid_t msgid = msgid_factory_.New();
  SendCommon(type, msgid, p, len);
  EntryRequest(msgid, cb, default_timeout_ts_);
}
void SimpleRPCStreamHandler::CallEx(uint16_t type, const void *p, qrpc_size_t len, qrpc_rpc_opt_t &opt) {
  qrpc_msgid_t msgid = msgid_factory_.New();
  SendCommon(type, msgid, p, len);
  EntryRequest(msgid, opt.callback, opt.timeout);
}

void SimpleRPCStreamHandler::Reply(qrpc_error_t result, qrpc_msgid_t msgid, const void *p, qrpc_size_t len) {
  ASSERT(result <= 0);
  //pack and send buffer
  char buffer[header_buff_len + len_buff_len + len];
  size_t ofs = 0;
  ofs = HeaderCodec::Encode(result, msgid, buffer, sizeof(buffer));
  ofs += LengthCodec::Encode(len, buffer + ofs, sizeof(buffer) - ofs);
  memcpy(buffer + ofs, p, len);
  WriteBytes(buffer, ofs + len);  
}



} //net
