#pragma once

#include <map>

#include "base/atomic.h"
#include "base/loop.h"
#include "base/handler_map.h"
#include "core/serial.h"

namespace qrpc {
class AlarmInterface;
class Loop : public base::Loop {
 public:
  Loop() : base::Loop(), 
          approx_now_in_usec_(0),
          alarm_map_(), 
          alarm_process_us_ts_(0),
          current_locked_session_id_(0),
          random_() {}

  inline void LockSession(SessionIndex idx) { current_locked_session_id_ = idx + 1; }
  inline void UnlockSession() { current_locked_session_id_ = 0; }
  inline bool IsSessionLocked(SessionIndex idx) const { return current_locked_session_id_ == (1 + idx); }

 public:
  //get/set
  Syscall::Random &random() { return random_; }

  //operations
  void Poll();
  uint64_t NowInUsec() const;
  void SetAlarm(AlarmInterface *a, uint64_t timeout_in_us);
  void CancelAlarm(AlarmInterface *a, uint64_t timeout_in_us);

 protected:
  class AlarmEntry {
   public:
    AlarmInterface *ptr_;
    bool erased_;
   public:
    AlarmEntry(AlarmInterface *ptr) : ptr_(ptr), erased_(false) {}
  };
  uint64_t approx_now_in_usec_;
  std::multimap<uint64_t, AlarmEntry> alarm_map_;
  qrpc_time_t alarm_process_us_ts_;
  atomic<NqSessionIndex> current_locked_session_id_;
  Syscall::Random random_;
};
} // net
