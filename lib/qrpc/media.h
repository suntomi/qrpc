#pragma once

#include "qrpc/base.h"

#include "base/media.h"

namespace qrpc {
  class Media : public base::Media {
  public:
    Media(const std::string &path, base::Serial::PartitionId pid, qrpc_media_handler_t &h) :
      base::Media(path, pid), handler_(h), context_(nullptr) {}
    int OnOpen() override { return qrpc_closure_call(handler_.on_media_open, ToHandle(), &context_); }
    void OnClose() override { qrpc_closure_call(handler_.on_media_close, ToHandle()); }
    void OnStateChange(const char *ev, const char *reason) override {
      qrpc_closure_call(handler_.on_media_state_change, ToHandle(), ev, reason);
    }
  protected:
    qrpc_media_handler_t handler_;
    void *context_;
  };
}