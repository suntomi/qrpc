#pragma once
#include "base/defs.h"

#include <functional>

namespace base {
  class AlarmProcessor {
  public:
      typedef std::function<qrpc_time_t ()> Handler;
      virtual int Set(const Handler &h, qrpc_time_t) = 0;
  };  
}