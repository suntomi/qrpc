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
    void Start(uint64_t timeout, uint64_t repeat = 0);
    void Stop();
    void Close();
    uint64_t GetTimeout() const
    {
      return this->timeout;
    }
    uint64_t GetRepeat() const
    {
      return this->repeat;
    }
    bool IsActive() const
    {
      return this->alarm_id != 0;
    }

  private:
    // Passed by argument.
    Listener* listener{ nullptr };
    AlarmProcessor &alarm_processor{ NopAlarmProcessor::Instance() };
    // Others.
    AlarmProcessor::Id alarm_id{ 0u };
    bool closed{ false };
    uint64_t timeout{ 0u };
    uint64_t repeat{ 0u };
  };
}

