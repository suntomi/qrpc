#pragma once

#include <cstdlib>

#include "base/alarm.h"
#include "base/loop_impl.h"
#include "base/io_processor.h"
#include "base/string.h"
#include "base/timer.h"

namespace base {
class Loop : public LoopImpl, IoProcessor {
  // because objects that behave as IoProcessor are allocated both on heap and stack,
  // using smart pointer like shared_ptr is not easy.
  IoProcessor **processors_{nullptr};
  TimerScheduler timer_;
  int max_nfd_{-1};
  LoopImpl::Timeout timeout_; // not initailized in constructor.
  // TODO: define default constructor of LoopImpl::Timeout in loop_impl.h
public:
  static const int kMinimumProcessorArraySize = 16;
  typedef LoopImpl::Event Event;
  Loop() : LoopImpl() {}
  ~Loop() { Close(); }
  template <class T> T *ProcessorAt(int fd) { return (T *)processors_[fd]; }
  inline AlarmProcessor &alarm_processor() { return timer_; }
  inline Fd fd() const { return LoopImpl::fd(); }
  inline int Open(int max_nfd, uint64_t timeout_ns = 1000 * 1000) {
    if (max_nfd < kMinimumProcessorArraySize) {
      max_nfd = kMinimumProcessorArraySize;
    }
    max_nfd_ = max_nfd; //TODO: use getrlimit if max_nfd omitted
    ToTimeout(timeout_ns, timeout_);
    processors_ = new IoProcessor*[max_nfd_];
    if (processors_ == nullptr) {
      QRPC_LOGJ(error, {{"ev","failed to allocate processors_"}, {"max_nfd", max_nfd}});
      ASSERT(false);
      return QRPC_EALLOC;
    }
    memset(processors_, 0, sizeof(IoProcessor*) * max_nfd_);
    return LoopImpl::Open(max_nfd_);
  }
  inline void Close() {
    if (processors_ != nullptr) {
      delete []processors_;
      processors_ = nullptr;
    }
    LoopImpl::Close();
  }
  inline int Add(Fd fd, IoProcessor *h, uint32_t flags) {
    CheckAndGrow(fd);
    ASSERT(processors_[fd] == nullptr);
    processors_[fd] = h;
    logger::info({{"ev","Loop::Add"}, {"fd", fd}, {"h", str::dptr(h)}});
    return LoopImpl::Add(fd, flags);
  }
  inline int Mod(Fd fd, uint32_t flags) {
    ASSERT(fd < max_nfd_ && processors_[fd] != nullptr);
    return LoopImpl::Mod(fd, flags);
  }
  inline void ModProcessor(Fd fd, IoProcessor *hnew) {
    ASSERT(fd < max_nfd_ && processors_[fd] != nullptr && hnew != nullptr);
    auto h = processors_[fd];
    if (h != hnew) {
      processors_[fd] = hnew;
    } else {
      ASSERT(false);
    }
  }
  inline int Del(Fd fd) {
    ASSERT(fd < max_nfd_ && processors_[fd] != nullptr);
    int r = LoopImpl::Del(fd);
    if (r >= 0) {
      auto h = processors_[fd];
      processors_[fd] = nullptr;
      logger::info({{"ev","Loop::Del"}, {"fd", fd}, {"h", str::dptr(h)}});
    } else {
      ASSERT(false);
    }
    return r;
  }
  inline int ForceDelWithCheck(Fd fd, IoProcessor *proc) {
    if (processors_[fd] == proc) {
      if (Del(fd) < 0) {
        // auto h = processors_[fd];
        processors_[fd] = nullptr;
      }
      return QRPC_OK;					
    } else {
      return QRPC_EGOAWAY; //already fd reused
    }
  }
  inline void Poll() {
    Event list[max_nfd_];
    int n_list = LoopImpl::Wait(list, max_nfd_, timeout_);
    for (int i = 0; i < n_list; i++) {
      const auto &ev = list[i];
      Fd fd = LoopImpl::From(ev);
      auto h = processors_[fd];
      h->OnEvent(fd, ev);
    }
    timer_.Poll();
  }
public: //IoProcessor
  void OnEvent(Fd lfd, const Event &e) override { ASSERT(fd() == lfd); Poll(); }

  inline void CheckAndGrow(Fd fd) {
    if ((int)fd >= max_nfd_) {
      int old = max_nfd_;
      do {
        max_nfd_ <<= 1;
      } while (max_nfd_ < (int)fd);
      processors_ = (IoProcessor**)std::realloc(processors_, max_nfd_ * sizeof(IoProcessor*));
      memset(processors_ + old, 0, sizeof(IoProcessor*) * (max_nfd_ - old));
    }
  }
};
}
