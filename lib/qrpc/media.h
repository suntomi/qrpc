#pragma once

#include "qrpc/base.h"

#include "base/media.h"

namespace qrpc {
  class Media : public base::Media {
  public:
    Media(const std::string &path, base::Connection &c, base::Serial::PartitionId pid, qrpc_media_handler_t &h) :
      base::Media(path, c, pid), handler_(h) {
        qrpc_closure_init_noop(consumer_, qrpc_on_media_consume_t);
      }
    int OnOpen() override { return qrpc_closure_call(handler_.on_media_open, ToHandle(), &context_); }
    void OnClose() override { qrpc_closure_call(handler_.on_media_close, ToHandle()); }
    void OnStateChange(const char *ev, const char *reason) override {
      qrpc_closure_call(handler_.on_media_state_change, ToHandle(), ev, reason);
    }
  protected:
    qrpc_media_handler_t handler_;
  };
}