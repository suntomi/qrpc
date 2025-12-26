#pragma once
#include "base/defs.h"

#include <functional>

namespace base {
  class AlarmProcessor {
  public:
    typedef uint64_t Id;
    constexpr static Id INVALID_ID = 0;
    typedef std::function<qrpc_time_t ()> Handler;
    virtual Id Set(const Handler &h, qrpc_time_t) = 0;
    virtual bool Cancel(Id id) = 0;
    qrpc_alarm_t ToHandle() {
      return reinterpret_cast<qrpc_alarm_t>(this);
    }
    static AlarmProcessor *FromHandle(qrpc_alarm_t al) {
      return reinterpret_cast<AlarmProcessor *>(al);
    }
  };
  class NopAlarmProcessor : public AlarmProcessor {
  public:
    static AlarmProcessor &Instance();
    Id Set(const Handler &, qrpc_time_t) override { ASSERT(false); return 0; }
    bool Cancel(Id) override { ASSERT(false); return false; }
  };
}