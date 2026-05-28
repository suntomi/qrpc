#pragma once

#include <signal.h>
#include <condition_variable>
#include <mutex>
#include <pthread.h>

#include "base/io_processor.h"
#include "base/loop.h"
#include "base/sig.h"
#include "qrpc.h"
#include "qrpc/worker.h"

namespace qrpc {

class SignalThread {
public:
  typedef Worker::Task Task;
  typedef Worker::TaskQueue TaskQueue;

  SignalThread() = default;
  ~SignalThread() = default;

  int Start();
  int Handle(int signum, qrpc_signal_handler_t handler);
  int Unhandle(int signum);

private:
  class WakeupProcessor : public base::IoProcessor {
  public:
    explicit WakeupProcessor(SignalThread &owner) : owner_(owner) {}
    void OnEvent(base::Fd fd, const Event &e) override;
  private:
    SignalThread &owner_;
  };

  void Run();
  int Wakeup();
  void Consume();
  bool ValidSignal(int signum) const;
  bool Ready();
  static void *ThreadMain(void *arg);

  base::Loop loop_;
  base::SignalHandler signal_;
  TaskQueue queue_;
  pthread_t thread_{};
  std::mutex init_mutex_;
  std::condition_variable init_cv_;
  base::Fd pipe_[2]{base::INVALID_FD, base::INVALID_FD};
  WakeupProcessor wakeup_{*this};
  bool handled_[NSIG]{};
  bool ready_{false};
  int init_result_{QRPC_OK};
  bool started_{false};

  DISALLOW_COPY_AND_ASSIGN(SignalThread);
};

int signal_init();
int signal_handle(int signum, qrpc_signal_handler_t handler);
int signal_unhandle(int signum);

} // namespace qrpc
