#pragma once
#include "base/alarm.h"
#include "base/io_processor.h"
#include "base/id_factory.h"
#include "base/logger.h"
#include "base/session.h"
#include "base/syscall.h"

namespace base {
  class Loop;
  class TimerScheduler : public IoProcessor, public AlarmProcessor {
  public:
    typedef std::function<qrpc_time_t ()> Handler;
    typedef AlarmProcessor::Id Id;
    static inline constexpr qrpc_time_t STOP = 0LL;
    struct Entry {
      Entry(uint64_t id, const Handler &h) : id(id), handler(h) {}
      Handler handler;
      Id id;
    };
  public:
    TimerScheduler(qrpc_time_t granularity) :
      fd_(INVALID_FD), granularity_(granularity),
      handlers_(), schedule_times_(), id_factory_() {}
    virtual ~TimerScheduler() {}
    int Init(Loop &l);
    void Fin();
    inline Fd fd() const { return fd_; }
    Id Start(const Handler &h, qrpc_time_t at);
    bool Stop(Id id);
    void Poll();
    // implement IoProcessor
    void OnEvent(Fd, const Event &) override;
		void OnClose(Fd) override { Fin(); }
    int OnOpen(Fd) override { return QRPC_OK; }
    // implement AlarmProcessor
    Id Set(const Handler &h, qrpc_time_t at) override {
      return Start(h, at);
    }
    bool Cancel(Id id) override { return Stop(id); }
  private:
    Fd fd_;
    qrpc_time_t granularity_;
    std::multimap<qrpc_time_t, Entry> handlers_;
    std::map<uint64_t, qrpc_time_t> schedule_times_;
    IdFactory<Id> id_factory_;
  };
}