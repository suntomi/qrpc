#pragma once
#include "base/defs.h"

#include <functional>

namespace base {
  class AlarmProcessor {
  public:
    typedef uint64_t Id;
    typedef std::function<qrpc_time_t ()> Handler;
    virtual Id Set(const Handler &h, qrpc_time_t) = 0;
    virtual bool Cancel(Id id) = 0;
  };
  class NopAlarmProcessor : public AlarmProcessor {
  public:
    static AlarmProcessor &Instance();
    Id Set(const Handler &, qrpc_time_t) override { ASSERT(false); return 0; }
    bool Cancel(Id) override { ASSERT(false); return false; }
  };
}