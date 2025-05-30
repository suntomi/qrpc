#include "qrpc/stream.h"

namespace qrpc {
  // CodedByteStream
  int CodedByteStream::OnRead(const char *p, size_t sz) {
    //greedy read and called back
    parse_buffer_.append(p, sz);
    const char *pstr = parse_buffer_.c_str();
    size_t plen = parse_buffer_.length();
    qrpc_size_t reclen = 0, read_ofs = base::LengthCodec::Decode(&reclen, pstr, plen);
    if (reclen > 0 && (reclen + read_ofs) <= plen) {
      qrpc_closure_call(handler_.on_stream_record, this->ToHandle(), pstr + read_ofs, reclen);
      parse_buffer_.erase(0, reclen + read_ofs); // consume
    } else if (reclen == 0 && plen > LENGTH_BUFFER_SIZE) {
      //broken payload. should resolve payload length
      this->Close(QRPC_CLOSE_REASON_PROTOCOL, 0, "broken payload");
    }
  }

  // RawByteStream
  int RawByteStream::OnRead(const char *p, size_t sz) {
    qrpc_closure_call(handler_.on_stream_record, this->ToHandle(), p, sz); return QRPC_OK; 
  }

  // RPCStream
  void RPCStream::EntryRequest(qrpc_msgid_t msgid, qrpc_on_rpc_reply_t cb, qrpc_time_t timeout_duration_ts) {
    auto limit_ts = timeout_duration_ts + qrpc_time_now();
    auto pair = req_map_.emplace(msgid, *this, msgid, cb, limit_ts);
    if (!pair.second) {
      logger::die({{"ev","rpc msgid collision"},{"msgid",msgid}});
      return;
    }
    if (alarm_id_ != AlarmProcessor::INVALID_ID) {
      ap_.Set([this]() { return this->CheckTimeout(); }, limit_ts);
    }
  }
  bool RPCStream::CompleteRequest(std::unordered_map<qrpc_msgid_t, Request>::iterator it, bool from_timer) {
    req_map_.erase(it);
    if (req_map_.empty()) {
      if (!from_timer) {
        ap_.Cancel(alarm_id_);
        alarm_id_ = AlarmProcessor::INVALID_ID;
      }
      return true;
    }
    return false;
  }

  qrpc_time_t RPCStream::CheckTimeout() {
    if (req_map_.empty()) {
      return 0;
    }
    auto now = qrpc_time_now();
    qrpc_time_t next_check_ts = UINT64_MAX;
    for (auto it = req_map_.begin(); it != req_map_.end(); ) {
      auto cur = it++;
      auto req = it->second;
      if (req.limit_ts_ <= now) {
        qrpc_closure_call(req.on_reply_, ToHandle(), QRPC_ETIMEOUT, "", 0);
        if (CompleteRequest(cur, true)) { return 0; }
        // req_map_.erase(cur);
      } else {
        next_check_ts = std::min(next_check_ts, req.limit_ts_ - now);
      }
    }
    return next_check_ts;
  }
  int RPCStream::OnRead(const char *p, size_t sz) {
    //TRACE("stream %llx handler OnRecv %u bytes", stream_->delegate()->SessionSerial().data[0], len);
    //greedy read and called back
    parse_buffer_.append(p, sz);
    //prepare tmp variables
    const char *pstr = parse_buffer_.c_str();
    size_t plen = parse_buffer_.length(), read_ofs;
    int16_t type_tmp; qrpc_msgid_t msgid; qrpc_size_t reclen;
    qrpc_error_t type;
    do {
      //decode header
      read_ofs = base::HeaderCodec::Decode(&type_tmp, &msgid, pstr, plen);
      /* tmp_ofs => length of encoded header, reclen => actual payload length */
      auto tmp_ofs = base::LengthCodec::Decode(&reclen, pstr + read_ofs, plen - read_ofs);
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
            //reply from serve side
            qrpc_closure_call(req.on_reply_, ToHandle(), type, pstr, reclen);
            CompleteRequest(it);
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
          qrpc_closure_call(rpc_.on_rpc_request, ToHandle(), type, msgid, pstr, reclen);
        }
      } else if (type > 0) {
        //notify
        //TRACE("stream handler notify: type %u", type);
        qrpc_closure_call(rpc_.on_rpc_notify, ToHandle(), type, pstr, reclen);
      } else {
        ASSERT(false);
      }
      parse_buffer_.erase(0, reclen + read_ofs);
      pstr = parse_buffer_.c_str();
      plen = parse_buffer_.length();
    } while (parse_buffer_.length() > 0);
  }
  int RPCStream::OnConnect() {
    return qrpc_closure_call(rpc_.on_rpc_open,  ToHandle(), &ctx_);
  }
  void RPCStream::OnShutdown() {
    qrpc_closure_call(rpc_.on_rpc_close, ToHandle()); 
  }
  void RPCStream::Close(const CloseReason &r) {
    for (auto it = req_map_.begin(); it != req_map_.end(); ) {
      auto cur = it++;
      auto req = cur->second;
      req.GoAway();
    }
    Stream::Close(r);
  }
  void RPCStream::Notify(uint16_t type, const void *p, qrpc_size_t len) {
    ASSERT(type > 0);
    //pack and send buffer
    char buffer[HEADER_BUFFER_SIZE + LENGTH_BUFFER_SIZE + len];
    size_t ofs = 0;
    ofs = base::HeaderCodec::Encode(static_cast<int16_t>(type), 0, buffer, sizeof(buffer));
    ofs += base::LengthCodec::Encode(len, buffer + ofs, sizeof(buffer) - ofs);
    memcpy(buffer + ofs, p, len);
    Send(buffer, ofs + len);
  }
  void RPCStream::Call(uint16_t type, const void *p, qrpc_size_t len, qrpc_on_rpc_reply_t cb) {
    qrpc_msgid_t msgid = msgid_factory_.New();
    SendCommon(type, msgid, p, len);
    EntryRequest(msgid, cb, default_timeout_ts_);
  }
  void RPCStream::CallEx(uint16_t type, const void *p, qrpc_size_t len, qrpc_rpc_opt_t &opt) {
    qrpc_msgid_t msgid = msgid_factory_.New();
    SendCommon(type, msgid, p, len);
    EntryRequest(msgid, opt.callback, opt.timeout);
  }
  void RPCStream::Reply(qrpc_error_t result, qrpc_msgid_t msgid, const void *p, qrpc_size_t len) {
    ASSERT(result <= 0);
    //pack and send buffer
    char buffer[HEADER_BUFFER_SIZE + LENGTH_BUFFER_SIZE + len];
    size_t ofs = 0;
    ofs = base::HeaderCodec::Encode(result, msgid, buffer, sizeof(buffer));
    ofs += base::LengthCodec::Encode(len, buffer + ofs, sizeof(buffer) - ofs);
    memcpy(buffer + ofs, p, len);
    Send(buffer, ofs + len);
  }
}