#pragma once

#include "base/id_factory.h"
#include "base/logger.h"
#include "base/loop.h"
#include "base/timer.h"
#include "base/alarm.h"

namespace qrpc {
  template <typename T>
  using IdFactory = base::IdFactory<T>;
  using Loop = base::Loop;
  namespace logger = base::logger;
  using Timer = base::TimerScheduler;
  using AlarmProcessor = base::AlarmProcessor;
}