#pragma once

#include "base/session.h"

#include "RTC/SctpDictionaries.hpp"

#include <stdint.h>
#include <functional>

namespace base {
  class Stream {
  public:
    class Processor {
    public:
      virtual ~Processor() = default;
    public:
      virtual void Close(Stream *) = 0;
      virtual int Send(Stream *, const char *, size_t, bool) = 0;
    };
  public:
    typedef RTC::SctpStreamParameters Config;
    typedef Session::CloseReason CloseReason;
    typedef uint16_t Id;
    typedef std::function<int (Stream &, const char *, size_t)> Handler;
  public:
    Stream(Processor *p, Config &c, Handler &h) : 
      handler_(h), processor_(p), config_(c), close_reason_(nullptr) {}
    virtual ~Stream() { if (close_reason_ != nullptr) { delete close_reason_; } }
    const Config &config() const { return config_; }
    bool closed() const { return close_reason_ != nullptr; }
    Id id() const { return config_.streamId; }
  public:
    virtual void Close(const CloseReason &reason) {
      if (!closed()) {
        close_reason_ = new CloseReason(reason);
        OnShutdown();
        processor_->Close(this);
      }
    }
    virtual int Send(const char *data, size_t sz) {
      return processor_->Send(this, data, sz, true);
    }
    virtual int OnConnect() { return QRPC_OK; }
    virtual void OnShutdown() {}
    inline int OnRead(const char *p, size_t sz) {
      return handler_(*this, p, sz);
    }
  private:
    Handler handler_;
    Processor *processor_;
    Config config_;
    CloseReason *close_reason_;
  };
}
