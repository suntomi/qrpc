#define MS_CLASS "Timer"
// #define MS_LOG_DEV_LEVEL 3

#include "base/webrtc/timer.h"
#include "base/webrtc/mpatch.h"
#include "MediaSoupErrors.hpp"

namespace base {
namespace webrtc {
  /* Instance methods. */

  Timer::Timer(Listener* listener, AlarmProcessor &ap) : listener(listener), alarm_processor(ap)
  {
    MS_TRACE();
  }

  Timer::~Timer()
  {
    MS_TRACE();
    Close();
  }

  void Timer::Close()
  {
    MS_TRACE();

    if (this->closed)
    {
      return;
    }

    Stop();

    this->closed = true;
  }

  void Timer::Start(uint64_t timeout)
  {
    MS_TRACE();

    if (this->closed)
    {
      MS_THROW_ERROR("closed");
    }

    Stop();

    this->alarm_id = this->alarm_processor.Set([this]() {
      // set alarm_id to 0 to prevent Stop() which is called in OnTimer() from calling AlarmProcessor::Cancal(),
      // which causes assertion in TimerScheduler::Stop(). the alarm will be canceled by return value (return 0)
      this->alarm_id = 0;
      this->listener->OnTimer(this); 
      return 0;
    }, qrpc_time_now() + qrpc_time_msec(timeout));
  }

  void Timer::Stop()
  {
    MS_TRACE();

    if (this->closed)
    {
      MS_THROW_ERROR("closed");
    }

    if (this->alarm_id != 0) {
      this->alarm_processor.Cancel(this->alarm_id);
      this->alarm_id = 0;
    }
  }
}  // namespace webrtc
}  // namespace base