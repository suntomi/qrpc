#pragma once

#include "base/defs.h"
#include "base/alarm.h"
#include "base/syscall.h"

#include "RTC/SctpAssociation.hpp"
#include "DepUsrSCTP.hpp"
#include "moodycamel/concurrentqueue.h"

#include <mutex>

namespace base {
  class AlarmProcessor;
  class SctpSender {
  public:
    struct Data {
      void* addr;
      void* data;
      size_t len;
    };
    static constexpr size_t kMinQueueSize = 256;
    static constexpr size_t kInitialThreadSize = 8;
    static thread_local moodycamel::ConcurrentQueue<Data> sctp_send_queue_;
    static thread_local AlarmProcessor::Id sctp_send_queue_alarm_id_;
    static std::vector<moodycamel::ConcurrentQueue<Data>*> thread_queue_map_;
    static void ClassInit(AlarmProcessor &a) {
      if (sctp_send_queue_alarm_id_ == AlarmProcessor::INVALID_ID) {
        auto thread_id = 0;
        for (size_t i = 0; i < thread_queue_map_.size(); ++i) {
          if (thread_queue_map_[i] == nullptr) {
            thread_id = i + 1;
            break;
          }
        }
        if (thread_id >= thread_queue_map_.size()) {
          thread_queue_map_.resize(thread_queue_map_.size() << 1);
          QRPC_LOGJ(info, {{"ev", "resizing thread queue map"},{"size",thread_queue_map_.size()}});
        }
        QRPC_LOGJ(info, {{"ev", "thread_id decided"},{"thread_id",thread_id}});
        RTC::SctpAssociation::SetSctpThreadId(thread_id);
        thread_queue_map_[RTC::SctpAssociation::GetSctpThreadId() - 1] = &sctp_send_queue_;
        if ((sctp_send_queue_alarm_id_ = a.Set([]() {
          Poll();
          return qrpc_time_now();
        }, 0)) == AlarmProcessor::INVALID_ID) {
          logger::die({{"ev","Failed to set SCTP send queue alarm"}});
        }
      } else {
        QRPC_LOGJ(debug, {{"ev", "already initialized"},{"thread_id", RTC::SctpAssociation::GetSctpThreadId()}});
      }
    }
    static void ClassDestroy(AlarmProcessor &a) {
      if (sctp_send_queue_alarm_id_ != AlarmProcessor::INVALID_ID) {
        a.Cancel(sctp_send_queue_alarm_id_);
        sctp_send_queue_alarm_id_ = AlarmProcessor::INVALID_ID;
        thread_queue_map_[RTC::SctpAssociation::GetSctpThreadId() - 1] = nullptr;
        RTC::SctpAssociation::SetSctpThreadId(0);
      }
    }
    static int onSendStcpData(void* addr, void* data, size_t len, uint8_t /*tos*/, uint8_t /*setDf*/) {
      // top 2 bytes of addr are the thread id
      auto threadId = reinterpret_cast<uintptr_t>(addr) >> (8 * (sizeof(uintptr_t) - 2));
      ASSERT(threadId > 0 && threadId <= thread_queue_map_.size());
      thread_queue_map_[threadId - 1]->enqueue({addr, Syscall::Memdup(data, len), len});
      return 0;
    }
    static void Poll() {
      auto &q = *thread_queue_map_[RTC::SctpAssociation::GetSctpThreadId() - 1];
      Data d;
      while (q.try_dequeue(d)) {
        ASSERT(d.data != nullptr); // Syscall::Memdup() fails
        auto *a = DepUsrSCTP::RetrieveSctpAssociation(reinterpret_cast<uintptr_t>(d.addr));
        if (a) { a->OnUsrSctpSendSctpData(d.data, d.len); }
        Syscall::MemFree(d.data);
      }
    }
  };
}