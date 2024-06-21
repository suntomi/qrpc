#include "base/timer.h"
#include "base/loop.h"

#if defined(__ENABLE_EPOLL__)
#include <timerfd.h>
#elif defined(__ENABLE_KQUEUE__)
#include <sys/event.h>
#endif

namespace base {
  int TimerScheduler::Init(Loop &l, qrpc_time_t granularity) {
    if (fd_ != INVALID_FD) {
      logger::error({{"ev","timer: already initialized"},{"fd",fd_}});
      return QRPC_EINVAL;
    }
    if (granularity_ == 0) {
      logger::error({{"ev","timer: granularity cannot be less than 0"},{"granularity",granularity}});
      return QRPC_EINVAL;
    }
    granularity_ = granularity;
  #if defined(__ENABLE_EPOLL__)
    if ((fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK)) < 0) {
      logger::error({{"ev","timerfd_create fails"},{"errno",Syscall::Errno()}});
      return QRPC_ESYSCALL;
    }
    struct itimespec itv;
    auto spec = qrpc_time_to_spec(granularity_);
    itv.it_interval.tv_sec = spec[0];
    itv.it_interval.tv_nsec = spec[1];
    auto ispec = qrpc_time_to_spec(0);
    itv.it_value.tv_sec = ispec[0];
    itv.it_value.tv_nsec = ispec[1];
    if (timerfd_settime(fd_, 0, &itv, nullptr) < 0) {
      logger::error({{"ev","timerfd_settime fails"},{"errno",Syscall::Errno()}});
      return QRPC_ESYSCALL;
    }
  #elif defined(__ENABLE_KQUEUE__)
    if (fd_ < 0) {
      fd_ = ::kqueue();
      if (fd_ < 0) {
        logger::error({{"ev","timer: kqueue() fails"},{"rv",fd_},{"errno",Syscall::Errno()}});
        return QRPC_ESYSCALL;
      }
    }
    struct kevent change;
    EV_SET(&change, 1, // TODO: if register multiple timers, this should be different id
      EVFILT_TIMER, EV_ADD | EV_ENABLE, NOTE_NSECONDS, qrpc_time_to_nsec(granularity_), this);
    if (::kevent(fd_, &change, 1, nullptr, 0, nullptr) != 0) {
      logger::error({{"ev","timer: kevent() fails"},{"errno",Syscall::Errno()}});
      return QRPC_ESYSCALL;
    }
  #endif
    if (l.Add(fd_, this, Loop::EV_READ) < 0) {
      logger::error({{"ev","timer: Loop::Add() fails"},{"fd",fd_},{"errno",Syscall::Errno()}});
      return QRPC_ESYSCALL;
    }
    return QRPC_OK;    
  }
  void TimerScheduler::Fin() {
    if (fd_ != INVALID_FD) {
      Syscall::Close(fd_);
      fd_ = INVALID_FD;
    }
  }
  TimerScheduler::Id TimerScheduler::Start(const Handler &h, qrpc_time_t at) {
    Id id = id_factory_.New();
    handlers_.insert(std::make_pair(at, Entry(id, h)));
    schedule_times_.insert(std::make_pair(id, at));
    logger::debug({{"ev","timer: create"},{"tid",id},{"ptr",str::dptr(this)}});
    return id;
  }
  bool TimerScheduler::Stop(Id id) {
    if (processed_now_ == id) {
      logger::warn({
        {"ev","try stop current processed timer. try using return 0 to stop processed timer"},
        {"tid",id}});
      ASSERT(false);
      return true;
    }
    auto i = schedule_times_.find(id);
    if (i == schedule_times_.end()) {
      logger::warn({{"ev","timer: id not found"},{"tid",id},{"ptr",str::dptr(this)}});
      ASSERT(false);
      return false;
    }
    ASSERT(i->second != 0 && i->first != 0);
    auto range = handlers_.equal_range(i->second);
    for (auto &j = range.first; j != range.second; ++j) {
      Entry &e = j->second;
      if (e.id == id) {
        logger::debug({{"ev","timer: stopped"},{"tid",id}});
        handlers_.erase(j);
        schedule_times_.erase(id);
        return true;
      }
    }
    logger::warn({{"ev","timer: handler entry not found"},{"tid",id},{"time",i->second}});
    schedule_times_.erase(id);
    ASSERT(false);
    return false;
  }

  // implement IoProcessor
  void TimerScheduler::OnEvent(Fd fd, const Event &ev) {
    ASSERT(fd == fd_);
    if (Loop::Readable(ev)) {
      while (true) {
        uint64_t expires;
      #if defined(__ENABLE_EPOLL__)
        if (Syscall::Read(fd_, &expires, sizeof(expires)) < 0) {
          if (Syscall::WriteMayBlocked(Syscall::Errno(), false)) {
            break;
          }
          logger::error({{"ev","read timerfd fails"},{"errno",Syscall::Errno()}});
          ASSERT(false);
          break;
        }
      #elif defined(__ENABLE_KQUEUE__)
        // poll kqueue fd
        Event ev[1];
        int r;
        Loop::Timeout to;
        Loop::ToTimeout(1000, to);
        if ((r = ::kevent(fd_, nullptr, 0, ev, 1, &to)) <= 0) {
          if (r == 0) {
            break;
          }
          logger::error({{"ev","kevent() fails"},{"rv",r},{"errno",Syscall::Errno()}});
          ASSERT(false);
          break;
        }
        expires = ev[0].data;
        // now not used
        (void) expires;
      #endif
      }
    }
    Poll();
  }
  void TimerScheduler::Poll() {
    qrpc_time_t now = qrpc_time_now();
    ASSERT(processed_now_ == 0);
    for (auto it = handlers_.begin(); it != handlers_.end(); ) {
      auto &ent = *it;
      if (ent.first > now) {
        break;
      }
      ASSERT(schedule_times_.find(ent.second.id) != schedule_times_.end() && 
        (*schedule_times_.find(ent.second.id)).second == ent.first);
      auto e = std::move(ent.second);
      processed_now_ = e.id;
      it = handlers_.erase(it);
      schedule_times_.erase(processed_now_);
      qrpc_time_t next = e.handler();
      if (next < now) {
        continue;
      }
      handlers_.insert(std::make_pair(next, e));
      schedule_times_.insert(std::make_pair(processed_now_, next));
    }
    processed_now_ = 0;
  }

}