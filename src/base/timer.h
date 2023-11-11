#pragma once
#include "base/alarm.h"
#include "base/io_processor.h"
#include "base/logger.h"
#include "base/session.h"
#include "base/syscall.h"

namespace base {
  class Loop;
  class Timer : public IoProcessor, public AlarmProcessor {
  public:
    typedef std::function<qrpc_time_t ()> Handler;
    static inline constexpr qrpc_time_t STOP = 0LL;
  public:
    Timer(qrpc_time_t granularity) : fd_(INVALID_FD), granularity_(granularity) {}
    virtual ~Timer() {}
    int Init(Loop &l);
    void Fin();
    inline Fd fd() const { return fd_; }
    int Start(const Handler &h, qrpc_time_t at);
    // implement IoProcessor
    void OnEvent(Fd, const Event &) override;
		void OnClose(Fd) override { Fin(); }
    int OnOpen(Fd) override { return QRPC_OK; }
    // implement AlarmProcessor
    int Set(const Handler &h, qrpc_time_t at) override {
      return Start(h, at);
    }
  private:
    Fd fd_;
    qrpc_time_t granularity_;
    std::multimap<qrpc_time_t, Handler> handlers_;
  };
}