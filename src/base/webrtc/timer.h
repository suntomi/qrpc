#pragma once

#include "base/alarm.h"
#include <stdint.h>

namespace base {
  class Timer
  {
  public:
    class Listener
    {
    public:
      virtual ~Listener() = default;

    public:
      virtual void OnTimer(Timer* timer) = 0;
    };

  public:
    explicit Timer(Listener* listener, AlarmProcessor &ap);
    Timer& operator=(const Timer&) = delete;
    Timer(const Timer&)            = delete;
    ~Timer();

  public:
    void Start(uint64_t timeout);
    void Stop();
    void Close();

  private:
    // Passed by argument.
    Listener* listener{ nullptr };
    AlarmProcessor &alarm_processor{ NopAlarmProcessor::Instance() };
    // Others.
    AlarmProcessor::Id alarm_id{ 0u };
    bool closed{ false };
  };
}

