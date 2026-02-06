#include "base/rtp/router.h"
#include "base/rtp/handler.h"

namespace base {
namespace rtp {
  void Router::OnTransportProducerRtpPacketReceived(
      RTC::Transport* transport, RTC::Producer* producer, RTC::RtpPacket* packet) {
    auto p = dynamic_cast<base::rtp::Producer *>(producer);
    ASSERT(p != nullptr);
    p->media()->OnRtpPacketReceived(
      reinterpret_cast<const char *>(packet->GetData()), packet->GetSize());
    RTC::Router::OnTransportProducerRtpPacketReceived(transport, producer, packet);
  }
} // namespace rtp
} // namespace base