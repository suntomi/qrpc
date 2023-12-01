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
      virtual void Close(Stream &) = 0;
      virtual int Send(Stream &, const char *, size_t, bool) = 0;
      virtual int Open(Stream &) = 0;
    };
  public:
    typedef struct {
      RTC::SctpStreamParameters params;
      std::string label;
      std::string protocol; // now not used
    } Config;
    typedef Session::CloseReason CloseReason;
    typedef uint16_t Id;
    typedef std::function<int (Stream &, const char *, size_t)> Handler;
  public:
    Stream(Processor &p, const Config &c) : 
      processor_(p), config_(c), close_reason_(nullptr) {}
    virtual ~Stream() {}
    const Config &config() const { return config_; }
    bool closed() const { return close_reason_ != nullptr; }
    Id id() const { return config_.params.streamId; }
    const std::string &label() const { return config_.label; }
  public:
    virtual int Open() {
      return processor_.Open(*this);
    }
    virtual void Close(const CloseReason &reason) {
      if (!closed()) {
        close_reason_ = std::make_unique<CloseReason>(reason);
        OnShutdown();
        processor_.Close(*this);
      }
    }
    virtual int Send(const char *data, size_t sz) {
      return processor_.Send(*this, data, sz, true);
    }
    virtual int OnConnect() { return QRPC_OK; }
    virtual void OnShutdown() {}
    virtual int OnRead(const char *p, size_t sz) = 0;
  private:
    Processor &processor_;
    Config config_;
    std::unique_ptr<CloseReason> close_reason_;
  };
  class AdhocStream : public Stream {
  public:
    typedef std::function<int (Stream &, const char *, size_t)> Handler;
  public:
    AdhocStream(Processor &p, const Config &c, const Handler &h) : Stream(p, c), handler_(h) {}
    int OnRead(const char *p, size_t sz) override {
      return handler_(*this, p, sz);
    }
  private:
    Handler handler_;
  };
}
