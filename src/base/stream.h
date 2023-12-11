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
      virtual int Open(Stream &) = 0;
      virtual void Close(Stream &) = 0;
      virtual int Send(Stream &, const char *, size_t, bool) = 0;
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
  protected:
    Processor &processor_;
    Config config_;
    std::unique_ptr<CloseReason> close_reason_;
  };
  class AdhocStream : public Stream {
  public:
    typedef std::function<int (Stream &)> ConnectHandler;
    typedef std::function<void (Stream &, const CloseReason &)> ShutdownHandler;
  public:
    AdhocStream(Processor &p, const Config &c, const Handler &h) :
      Stream(p, c), read_handler_(h), connect_handler_(Nop()), shutdown_handler_(Nop()) {}
    AdhocStream(Processor &p, const Config &c, 
      const Handler &rh, const ConnectHandler &ch, const ShutdownHandler &sh) :
        Stream(p, c), read_handler_(rh), connect_handler_(ch), shutdown_handler_(sh) {}
    int OnRead(const char *p, size_t sz) override { return read_handler_(*this, p, sz); }
    int OnConnect() override { return connect_handler_(*this); }
    void OnShutdown() override { return shutdown_handler_(*this, *close_reason_); }
  private:
    struct Nop {
      int operator()(Stream &) { return QRPC_OK; }
      void operator()(Stream &, const CloseReason &) {}
    };
  private:
    Handler read_handler_;
    ConnectHandler connect_handler_;
    ShutdownHandler shutdown_handler_;
  };
}
