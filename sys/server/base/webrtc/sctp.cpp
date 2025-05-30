#include "base/webrtc/sctp.h"

namespace base {
  thread_local moodycamel::ConcurrentQueue<SctpSender::Data> SctpSender::sctp_send_queue_(SctpSender::kMinQueueSize, 1, 0);
  thread_local AlarmProcessor::Id SctpSender::sctp_send_queue_alarm_id_ = AlarmProcessor::INVALID_ID;
  std::vector<moodycamel::ConcurrentQueue<SctpSender::Data>*> SctpSender::thread_queue_map_(SctpSender::kInitialThreadSize);
  std::mutex SctpSender::sctp_send_queue_mutex_;
}