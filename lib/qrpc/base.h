#pragma once

#include "base/id_factory.h"
#include "base/logger.h"
#include "base/timer.h"
#include "base/alarm.h"
#include "base/resolver.h"
#include "qrpc/loop.h"

namespace qrpc {
  template <typename T>
  using IdFactory = base::IdFactory<T>;
  namespace logger = base::logger;
  using Timer = base::TimerScheduler;
  using AlarmProcessor = base::AlarmProcessor;
  using AsyncResolver = base::AsyncResolver;
}
