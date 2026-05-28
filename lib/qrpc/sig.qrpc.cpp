#include "qrpc/sig.h"

#include <signal.h>
#include <unistd.h>

#include "base/logger.h"
#include "base/syscall.h"

namespace qrpc {

namespace {
SignalThread g_signal_thread;
}

int SignalThread::Start() {
  if (started_) { return QRPC_OK; }
  if (::pipe(pipe_) != 0) {
    QRPC_LOGJ(error, {{"ev","signal pipe() failed"},{"errno",base::Syscall::Errno()}});
    return QRPC_ESYSCALL;
  }
  if (!base::Syscall::SetNonblocking(pipe_[0]) || !base::Syscall::SetNonblocking(pipe_[1])) {
    base::Syscall::Close(pipe_[0]);
    base::Syscall::Close(pipe_[1]);
    pipe_[0] = pipe_[1] = base::INVALID_FD;
    return QRPC_ESYSCALL;
  }
  {
    std::lock_guard<std::mutex> lock(init_mutex_);
    ready_ = false;
    init_result_ = QRPC_OK;
  }
  started_ = true;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  size_t stack_size = 64 * 1024;
  if (stack_size < PTHREAD_STACK_MIN) {
    stack_size = PTHREAD_STACK_MIN;
  }
  pthread_attr_setstacksize(&attr, stack_size);
  int r = pthread_create(&thread_, &attr, &SignalThread::ThreadMain, this);
  pthread_attr_destroy(&attr);
  if (r != 0) {
    base::Syscall::Close(pipe_[0]);
    base::Syscall::Close(pipe_[1]);
    pipe_[0] = pipe_[1] = base::INVALID_FD;
    started_ = false;
    return QRPC_ESYSCALL;
  }
  std::unique_lock<std::mutex> lock(init_mutex_);
  init_cv_.wait(lock, [this]() { return ready_; });
  if (init_result_ < 0) {
    lock.unlock();
    pthread_join(thread_, nullptr);
    base::Syscall::Close(pipe_[0]);
    base::Syscall::Close(pipe_[1]);
    pipe_[0] = pipe_[1] = base::INVALID_FD;
    started_ = false;
  } else {
    pthread_detach(thread_);
  }
  return init_result_;
}

int SignalThread::Handle(int signum, qrpc_signal_handler_t handler) {
  if (!Ready() || !ValidSignal(signum) || qrpc_closure_is_empty(handler)) { return QRPC_EINVAL; }
  queue_.enqueue([this, signum, handler]() {
    signal_.Handle(signum, [handler, this](int signum, const ::Signal &s) {
      qrpc_signal_event_t ev = {
        .signum = signum,
        .reap_count = signal_.GetReapCount(s),
      };
      qrpc_closure_call(handler, &ev);
    });
    handled_[signum] = true;
  });
  return Wakeup();
}

int SignalThread::Unhandle(int signum) {
  if (!Ready() || !ValidSignal(signum)) { return QRPC_EINVAL; }
  queue_.enqueue([this, signum]() {
    if (!handled_[signum]) { return; }
    signal_.Unhandle(signum);
    handled_[signum] = false;
  });
  return Wakeup();
}

void SignalThread::Run() {
  int init_result = QRPC_OK;
  if (loop_.Open(base::Loop::kMinimumProcessorArraySize) < 0) {
    QRPC_LOGJ(error, {{"ev","signal Loop::Open() failed"}});
    init_result = QRPC_ESYSCALL;
  } else if (loop_.Add(pipe_[0], &wakeup_, base::Loop::EV_READ) < 0) {
    QRPC_LOGJ(error, {{"ev","signal wakeup Loop::Add() failed"},{"fd",pipe_[0]}});
    init_result = QRPC_ESYSCALL;
  } else if (signal_.Open() < 0) {
    QRPC_LOGJ(error, {{"ev","signal SignalHandler::Open() failed"}});
    init_result = QRPC_ESYSCALL;
  } else if (!signal_.Start(loop_)) {
    QRPC_LOGJ(error, {{"ev","signal SignalHandler::Start() failed"},{"fd",signal_.fd()}});
    init_result = QRPC_ESYSCALL;
  }
  {
    std::lock_guard<std::mutex> lock(init_mutex_);
    init_result_ = init_result;
    ready_ = true;
  }
  init_cv_.notify_one();
  if (init_result < 0) { return; }
  while (true) { loop_.WaitEvent(); }
}

void *SignalThread::ThreadMain(void *arg) {
  static_cast<SignalThread *>(arg)->Run();
  return nullptr;
}

int SignalThread::Wakeup() {
  uint8_t b = 1;
  int r = base::Syscall::Write(pipe_[1], &b, sizeof(b));
  if (r < 0 && !base::Syscall::IOMayBlocked(base::Syscall::Errno(), false)) {
    QRPC_LOGJ(error, {{"ev","signal wakeup write failed"},{"errno",base::Syscall::Errno()}});
    return QRPC_ESYSCALL;
  }
  return QRPC_OK;
}

void SignalThread::Consume() {
  Task t;
  while (queue_.try_dequeue(t)) { t(); }
}

bool SignalThread::ValidSignal(int signum) const {
  return signum > 0 && signum < NSIG;
}

bool SignalThread::Ready() {
  std::lock_guard<std::mutex> lock(init_mutex_);
  return ready_ && init_result_ == QRPC_OK;
}

void SignalThread::WakeupProcessor::OnEvent(base::Fd fd, const Event &e) {
  if (base::Loop::Readable(e)) {
    uint8_t buff[64];
    while (true) {
      int r = base::Syscall::Read(fd, buff, sizeof(buff));
      if (r < 0) {
        if (base::Syscall::IOMayBlocked(base::Syscall::Errno(), false)) { break; }
        QRPC_LOGJ(error, {{"ev","signal wakeup read failed"},{"errno",base::Syscall::Errno()}});
        break;
      }
      if (r == 0 || r < static_cast<int>(sizeof(buff))) { break; }
    }
  }
  owner_.Consume();
}

int signal_init() {
  return g_signal_thread.Start();
}

int signal_handle(int signum, qrpc_signal_handler_t handler) {
  return g_signal_thread.Handle(signum, handler);
}

int signal_unhandle(int signum) {
  return g_signal_thread.Unhandle(signum);
}

} // namespace qrpc
