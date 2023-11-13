#include "base/timer.h"

#if defined(__ENABLE_EPOLL__)
#include <timerfd.h>
#elif defined(__ENABLE_KQUEUE__)
#include <sys/event.h>
#endif

namespace base {
  #if defined(__ENABLE_KQUEUE__)
  IdFactory<uint64_t> Timer::id_factory_;
  #endif
  int Timer::Init(Loop &l) {
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
    EV_SET(&change, id_factory_.New(), 
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
  void Timer::Fin() {
    if (fd_ != INVALID_FD) {
      Syscall::Close(fd_);
      fd_ = INVALID_FD;
    }
  }
  int Timer::Start(const Handler &h, qrpc_time_t at) {
    handlers_.insert(std::make_pair(at, h));
    return QRPC_OK;
  }
  // implement IoProcessor
  void Timer::OnEvent(Fd fd, const Event &ev) {
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
        if ((r = ::kevent(fd_, nullptr, 0, ev, 1, &to)) < 0) {
          if (Syscall::WriteMayBlocked(Syscall::Errno(), false)) {
            break;
          }
          logger::error({{"ev","kevent() fails"},{"rv",r},{"errno",Syscall::Errno()}});
          ASSERT(false);
          break;
        } else if (r == 0) {
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
  void Timer::Poll() {
    qrpc_time_t now = qrpc_time_now();
    for (auto it = handlers_.begin(); it != handlers_.end(); ) {
      auto &ent = *it;
      if (ent.first > now) {
        break;
      }
      auto handler = std::move(ent.second);
      qrpc_time_t next = handler();
      it = handlers_.erase(it);
      if (next < now) {
        continue;
      }
      handlers_.insert(std::make_pair(next, handler));
    }
  }
}