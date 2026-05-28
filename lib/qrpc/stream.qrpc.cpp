#include "qrpc/stream.h"

namespace qrpc {
  namespace {
    template <class F>
    inline int ConsumeCodedRecords(const char *p, size_t sz, F &&on_record, size_t &consumed) {
      consumed = 0;
      while (consumed < sz) {
        qrpc_size_t reclen = 0;
        auto read_ofs = base::LengthCodec::Decode(&reclen, p + consumed, sz - consumed);
        if (read_ofs == 0) {
          auto remain = sz - consumed;
          return (remain > Stream::LENGTH_BUFFER_SIZE) ? QRPC_EINVAL : QRPC_EAGAIN;
        }
        auto total = static_cast<size_t>(read_ofs) + static_cast<size_t>(reclen);
        if (total > (sz - consumed)) {
          return QRPC_EAGAIN;
        }
        on_record(p + consumed + read_ofs, reclen);
        consumed += total;
      }
      return QRPC_OK;
    }
  }

  // ByteStream
  int ByteStream::OnRead(const char *p, size_t sz) {
    qrpc_closure_call(handler_.on_stream_record, this->ToHandle(), p, sz); return QRPC_OK; 
  }

  // CodedByteStream
  int CodedByteStream::Send(const char *data, size_t sz) {
    auto buflen = LENGTH_BUFFER_SIZE + sz;
    char *buffer = ALLOC_STACK_BUFFER(char, buflen);
    auto ofs = base::LengthCodec::Encode(sz, buffer, buflen);
    memcpy(buffer + ofs, data, sz);
    return Stream::Send(buffer, ofs + sz);
  }
  int CodedByteStream::OnRead(const char *p, size_t sz) {
    auto on_record = [this](const char *record, qrpc_size_t reclen) {
      qrpc_closure_call(handler_.on_stream_record, this->ToHandle(), record, reclen);
    };
    size_t consumed = 0;
    if (parse_buffer_.empty()) {
      auto rv = ConsumeCodedRecords(p, sz, on_record, consumed);
      if (rv == QRPC_OK) {
        return QRPC_OK;
      }
      if (rv == QRPC_EINVAL) {
        this->Close(QRPC_CLOSE_REASON_PROTOCOL, 0, "broken payload");
        return rv;
      }
      parse_buffer_.append(p + consumed, sz - consumed);
      return QRPC_OK;
    }
    parse_buffer_.append(p, sz);
    auto rv = ConsumeCodedRecords(parse_buffer_.data(), parse_buffer_.size(), on_record, consumed);
    if (consumed > 0) {
      parse_buffer_.erase(0, consumed);
    }
    if (rv == QRPC_EINVAL) {
      this->Close(QRPC_CLOSE_REASON_PROTOCOL, 0, "broken payload");
      return rv;
    }
    return QRPC_OK;
  }

  // RPCBase
  void RPCBase::EntryRequest(qrpc_msgid_t msgid, qrpc_on_rpc_reply_t cb, qrpc_time_t timeout_duration_ts) {
    auto limit_ts = timeout_duration_ts + qrpc_time_now();
    auto pair = req_map_.emplace(
      std::piecewise_construct, 
      std::make_tuple(msgid), std::make_tuple(this, msgid, cb, limit_ts)
    );
    if (!pair.second) {
      logger::die({{"ev","rpc msgid collision"},{"msgid",msgid}});
      return;
    }
    if (alarm_id_ != AlarmProcessor::INVALID_ID) {
      ap_.Set([this]() { return this->CheckTimeout(); }, limit_ts);
    }
  }
  bool RPCBase::CompleteRequest(std::unordered_map<qrpc_msgid_t, Request>::iterator it, bool from_timer) {
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

  qrpc_time_t RPCBase::CheckTimeout() {
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
  int RPCBase::ProcessRecord(const char *p, size_t sz) {
    size_t read_ofs;
    int16_t type_tmp;
    qrpc_msgid_t msgid;
    qrpc_error_t type;
    read_ofs = base::HeaderCodec::Decode(&type_tmp, &msgid, p, sz);
    if (read_ofs > sz) {
      Stream::Close({ .code = QRPC_CLOSE_REASON_PROTOCOL, .detail_code = 0, .msg = "broken rpc header" });
      return QRPC_EINVAL;
    }
    auto payload = p + read_ofs;
    auto payload_len = sz - read_ofs;
    type = static_cast<qrpc_error_t>(type_tmp);
    if (msgid != 0) {
      if (type <= 0) {
        auto it = req_map_.find(msgid);
        if (it != req_map_.end()) {
          auto req = it->second;
          qrpc_closure_call(req.on_reply_, ToHandle(), type, payload, payload_len);
          CompleteRequest(it);
        } else {
          // reply for an already timed out request
        }
      } else {
        qrpc_closure_call(rpc_.on_rpc_request, ToHandle(), type, msgid, payload, payload_len);
      }
    } else if (type > 0) {
      qrpc_closure_call(rpc_.on_rpc_notify, ToHandle(), type, payload, payload_len);
    } else {
      ASSERT(false);
    }
    return QRPC_OK;
  }
  int RPCStream::OnRead(const char *p, size_t sz) {
    return ProcessRecord(p, sz);
  }
  void RPCStream::SendCommon(int16_t type, qrpc_msgid_t msgid, const void *p, qrpc_size_t len) {
    auto buflen = HEADER_BUFFER_SIZE + len;
    char *buffer = ALLOC_STACK_BUFFER(char, buflen);
    auto ofs = base::HeaderCodec::Encode(type, msgid, buffer, buflen);
    memcpy(buffer + ofs, p, len);
    Send(buffer, ofs + len);
  }
  int CodedRPCStream::OnRead(const char *p, size_t sz) {
    auto on_record = [this](const char *record, qrpc_size_t reclen) {
      ProcessRecord(record, reclen);
    };
    size_t consumed = 0;
    if (parse_buffer_.empty()) {
      auto rv = ConsumeCodedRecords(p, sz, on_record, consumed);
      if (rv == QRPC_OK) {
        return QRPC_OK;
      }
      if (rv == QRPC_EINVAL) {
        Stream::Close({ .code = QRPC_CLOSE_REASON_PROTOCOL, .detail_code = 0, .msg = "broken rpc payload" });
        return rv;
      }
      parse_buffer_.append(p + consumed, sz - consumed);
      return QRPC_OK;
    }
    parse_buffer_.append(p, sz);
    auto rv = ConsumeCodedRecords(parse_buffer_.data(), parse_buffer_.size(), on_record, consumed);
    if (consumed > 0) {
      parse_buffer_.erase(0, consumed);
    }
    if (rv == QRPC_EINVAL) {
      Stream::Close({ .code = QRPC_CLOSE_REASON_PROTOCOL, .detail_code = 0, .msg = "broken rpc payload" });
      return rv;
    }
    return QRPC_OK;
  }
  void CodedRPCStream::SendCommon(int16_t type, qrpc_msgid_t msgid, const void *p, qrpc_size_t len) {
    auto buflen = HEADER_BUFFER_SIZE + LENGTH_BUFFER_SIZE + len;
    char *buffer = ALLOC_STACK_BUFFER(char, buflen);
    size_t ofs = 0;
    ofs = base::HeaderCodec::Encode(type, msgid, buffer, buflen);
    ofs += base::LengthCodec::Encode(len, buffer + ofs, buflen - ofs);
    memcpy(buffer + ofs, p, len);
    Send(buffer, ofs + len);
  }
  int RPCBase::OnConnect() {
    return qrpc_closure_call(rpc_.on_rpc_open,  ToHandle(), &ctx_);
  }
  void RPCBase::OnShutdown() {
    qrpc_closure_call(rpc_.on_rpc_close, ToHandle()); 
  }
  void RPCBase::Close(const CloseReason &r) {
    for (auto it = req_map_.begin(); it != req_map_.end(); ) {
      auto cur = it++;
      auto req = cur->second;
      req.GoAway();
    }
    Stream::Close(r);
  }
  void RPCBase::Notify(uint16_t type, const void *p, qrpc_size_t len) {
    ASSERT(type > 0);
    SendCommon(static_cast<int16_t>(type), 0, p, len);
  }
  void RPCBase::Call(uint16_t type, const void *p, qrpc_size_t len, qrpc_on_rpc_reply_t cb) {
    qrpc_msgid_t msgid = msgid_factory_.New();
    SendCommon(type, msgid, p, len);
    EntryRequest(msgid, cb, default_timeout_ts_);
  }
  void RPCBase::CallEx(uint16_t type, const void *p, qrpc_size_t len, const qrpc_rpc_opt_t &opt) {
    qrpc_msgid_t msgid = msgid_factory_.New();
    SendCommon(type, msgid, p, len);
    EntryRequest(msgid, opt.callback, opt.timeout);
  }
  void RPCBase::Reply(qrpc_error_t result, qrpc_msgid_t msgid, const void *p, qrpc_size_t len) {
    ASSERT(result <= 0);
    SendCommon(result, msgid, p, len);
  }
}
