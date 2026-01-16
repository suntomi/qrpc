#pragma once

#include "qrpc/base.h"

#include "base/media.h"

namespace qrpc {
  class Media : public base::Media {
  public:
    Media(const std::string &path, base::Serial::PartitionId pid, qrpc_media_handler_t &h) :
      base::Media(path, pid), handler_(h), context_(nullptr) {}
    virtual int OnOpen() { return qrpc_closure_call(handler_.on_media_open, ToHandle(), &context_); }
    virtual void OnClose() { qrpc_closure_call(handler_.on_media_close, ToHandle()); }
  protected:
    qrpc_media_handler_t handler_;
    void *context_;
  };
}