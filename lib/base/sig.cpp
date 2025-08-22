#include "base/sig.h"

#if defined(__ENABLE_EPOLL__)
#elif defined(__ENABLE_KQUEUE__)
#include <sys/event.h>
#endif

namespace base {
  int SignalHandler::Register(int sig) {
    if (sigaddset(&mask_, sig) != 0) {
      logger::error({{"ev","unsupported signal"},{"sig",sig},{"errno",Syscall::Errno()}});
      return QRPC_EINVAL;
    }
    // ignore signals that handled by signalfd
    if (sigprocmask(SIG_BLOCK, &mask_, NULL) != 0) {
      logger::error({{"ev","sigprocmask(BLOCK) fails"},
        {"mask",str::HexDump(reinterpret_cast<const uint8_t *>(&mask_), sizeof(mask_))},
        {"errno",Syscall::Errno()}});
      return QRPC_ESYSCALL;
    }
  #if defined(__ENABLE_EPOLL__)
    if ((fd_ =  signalfd(fd_, &mask_, SFD_NONBLOCK)) < 0) {
      logger::error({{"ev","signalfd() fails"},{"errno",Syscall::Errno()}});
      return QRPC_ESYSCALL;
    }
  #elif defined(__ENABLE_KQUEUE__)
    if (fd_ < 0) {
      fd_ = ::kqueue();
      if (fd_ < 0) {
        logger::error({{"ev","signal: kqueue() fails"},{"rv",fd_},{"errno",Syscall::Errno()}});
        return QRPC_ESYSCALL;
      }
    }
    int r;
    Event ev;
    EV_SET(&ev, sig, EVFILT_SIGNAL, EV_ADD, 0, 0, nullptr);
    if ((r = ::kevent(fd_, &ev, 1, nullptr, 0, nullptr)) != 0) {
      logger::error({{"ev","kevent() fails"},{"rv",r},{"errno",Syscall::Errno()}});
      return QRPC_ESYSCALL;
    }
  #endif
    return QRPC_OK;
  }
  void SignalHandler::OnEvent(Fd fd, const Event &e) {
    if (Loop::Readable(e)) {
      while (true) {
  #if defined(__ENABLE_EPOLL__)
        // read signalfd
        int r;
        Signal s;
        if ((r = Syscall::Read(fd_, &s, sizeof(s))) < 0) {
          if (Syscall::IOMayBlocked(Syscall::Errno(), false)) {
            break;
          }
          logger::error({{"ev","read signalfd fails"},{"errno",Syscall::Errno()}});
          ASSERT(false);
          break;
        }
        if (r != sizeof(s)) {
          logger::error({{"ev","invalid payload size"},{"size",r}});
          ASSERT(false);
          continue;
        }
        // invoke receiver
        receivers_[s.ssi_signo](s.ssi_signo, s);
  #elif defined(__ENABLE_KQUEUE__)
        // poll kqueue fd
        Event ev[NSIG];
        int r;
        Loop::Timeout to;
        Loop::ToTimeout(1000, to);
        if ((r = ::kevent(fd_, nullptr, 0, ev, NSIG, &to)) <= 0) {
          if (r == 0) {
            break;
          }
          logger::error({{"ev","kevent() fails"},{"rv",r},{"errno",Syscall::Errno()}});
          ASSERT(false);
          break;
        }
        for (int i = 0; i < r; i++) {
          // invoke receiver
          receivers_[ev[i].ident](ev[i].ident, ev[i]);
        }
  #endif
      }
    }
  }
  void SignalHandler::Fin() {
    // un-ignore signals that handled by signalfd
    if (sigprocmask(SIG_UNBLOCK, &mask_, NULL) != 0) {
      logger::error({{"ev","sigprocmask(UNBLOCK) fails"},
        {"mask",str::HexDump(reinterpret_cast<const uint8_t *>(&mask_), sizeof(mask_))},
        {"errno",Syscall::Errno()}});
    }
    if (fd_ != -1) {
      Syscall::Close(fd_);
      fd_ = -1;
    }
  }
}
