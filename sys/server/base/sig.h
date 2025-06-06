#pragma once
#include "base/loop.h"
#include "base/io_processor.h"
#include "base/logger.h"

#include <signal.h>
#include <functional>

#if defined(__ENABLE_EPOLL__)
#include <sys/signalfd.h>
typedef struct signalfd_siginfo Signal;
#elif defined(__ENABLE_KQUEUE__)
typedef base::Loop::Event Signal;
#else
// dummy definition to shut vscode language server up
typedef struct {} Signal;
#endif

#if !defined(SIGRTMAX)
// some OS (eg. darwin) does not seems to define SIGRTMAX
// we set SIGRTMAX to enough big value
#define SIGRTMAX 256
#endif

namespace base {
  class SignalHandler : public IoProcessor {
  public:
    typedef std::function<SignalHandler& (SignalHandler &)> Initializer;
    typedef std::function<void (int, const Signal &)> Receiver;
    // TODO: make it singleton
    // fd_ should be -1 so that signalfd(fd, ...) will create fd for first call of Register()
    SignalHandler() {
      for (int i = 0; i < SIGRTMAX; i++) { receivers_[i] = Nop(); }
      sigemptyset(&mask_);
    }
    virtual ~SignalHandler() { Fin(); }
    inline bool Init(Loop &l, const Initializer &init) { return init(*this).Start(l); }
    void Fin();
    inline Fd fd() const { return fd_; }
    inline SignalHandler &Ignore(int sig) {
      return Handle(sig, [](int sig, const Signal &s) {
        TRACEJ({{"ev","signal ignored"},{"sig",sig}});
      });
    }
    SignalHandler &Handle(int sig, const Receiver &r) {
      receivers_[sig] = r;
      int ret;
      if ((ret = Register(sig)) < 0) {
        logger::error({{"ev","register signal fails"},{"sig",sig},{"error",ret}});
        ASSERT(false);
        receivers_[sig] = Nop();
      }
      return *this;
    }
    bool Start(Loop &l) {
      if (l.Add(fd_, this, Loop::EV_READ) < 0) {
        logger::error({{"ev","sig: Loop::Add() fails"},{"fd",fd_},{"errno",Syscall::Errno()}});
        return false;
      }
      return true;
    }
    int Register(int);
    void OnEvent(Fd fd, const Event &e) override;
  protected:
    class Nop {
    public:
      void operator() (int, const Signal &) {} 
    };
  protected:
    Fd fd_{-1};
    Receiver receivers_[SIGRTMAX];
    sigset_t mask_; 
  };
}