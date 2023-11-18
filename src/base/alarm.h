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
}