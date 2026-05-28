#pragma once

#include "qrpc/base.h"
#include "qrpc/conn.h"
#include "qrpc/handle.h"

#include "base/media.h"

namespace qrpc {
  class Media : public base::Media {
  public:
    Media(const std::string &path, Direction d, base::Connection &c, qrpc_media_handler_t &h) :
      base::Media(path, d, c), handler_(h) {
        HandleBss::FromObject(this)->Reset(dynamic_cast<qrpc::Connection &>(c).partition_id());
        qrpc_closure_init_noop(consumer_, qrpc_on_media_consume_t);
      }
    const qrpc_serial_t &serial() const { return HandleBss::FromObject(this)->serial; }
    qrpc_media_t ToHandle() { return { .s = serial(), .p = this }; }
    static inline Media *FromHandle(qrpc_media_t media) {
      auto p = reinterpret_cast<const Media *>(media.p);
      if (p == nullptr || !base::Serial::IsSame(HandleBss::FromObject(p)->serial, media.s)) {
        return nullptr;
      }
      return const_cast<Media *>(p);
    }
    int OnOpen() override { return qrpc_closure_call(handler_.on_media_open, ToHandle(), &context_); }
    void OnClose() override { qrpc_closure_call(handler_.on_media_close, ToHandle()); }
    void OnStateChange(const char *ev, const char *reason) override {
      qrpc_closure_call(handler_.on_media_state_change, ToHandle(), ev, reason);
    }
    void OnRtpPacketReceived(const char *data, size_t len) override {
      qrpc_closure_call(consumer_, ToHandle(), data, len);
    }
  protected:
    qrpc_media_handler_t handler_;
  };
} 
