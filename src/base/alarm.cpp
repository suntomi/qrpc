#include "base/alarm.h"

namespace base {
  AlarmProcessor &NopAlarmProcessor::Instance() {
    static NopAlarmProcessor instance;
    return instance;
  }
}