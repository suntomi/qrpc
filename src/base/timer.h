#pragma once
#include "base/alarm.h"
#include "base/io_processor.h"
#include "base/id_factory.h"
#include "base/logger.h"

namespace base {
  class Loop;
  class TimerScheduler : public AlarmProcessor, public IoProcessor {
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
    TimerScheduler() : fd_(INVALID_FD), granularity_(0),
      handlers_(), schedule_times_(), id_factory_(),
      processed_now_(0) {}
    virtual ~TimerScheduler() { Fin(); }
    int Init(Loop &l, qrpc_time_t granularity);
    void Fin();
    inline Fd fd() const { return fd_; }
    Id Start(const Handler &h, qrpc_time_t at);
    bool Stop(Id id);
    void Poll();
    // implement IoProcessor
    void OnEvent(Fd, const Event &) override;
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
    Id processed_now_;
  };
}