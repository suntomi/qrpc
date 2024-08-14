#pragma once

#include "base/defs.h"
#include "base/media.h"

#include "RTC/Consumer.hpp"
#include "RTC/DataConsumer.hpp"
#include "RTC/DataProducer.hpp"
#include "RTC/Producer.hpp"
#include "RTC/RtpHeaderExtensionIds.hpp"
#include "RTC/RtpPacket.hpp"
#include "RTC/RateCalculator.hpp"
#ifdef ENABLE_RTC_SENDER_BANDWIDTH_ESTIMATOR
#include "RTC/SenderBandwidthEstimator.hpp"
#endif

#include <absl/container/flat_hash_map.h>

using json = nlohmann::json;

namespace RTC {
  class TransportCongestionControlClient;
  class TransportCongestionControlServer;
}

namespace base {
namespace rtp {
  class Handler {
  public:
    typedef const std::function<void(bool sent)> onSendCallback;  
    typedef RTC::RtpHeaderExtensionIds ExtensionIds;
    class Listener {
    public:
      virtual std::shared_ptr<Media> FindFrom(RTC::RtpPacket &packet) = 0;
      virtual void RecvStreamClosed(uint32_t ssrc) = 0;
      virtual void SendStreamClosed(uint32_t ssrc) = 0;
      virtual bool IsConnected() const = 0;
      virtual void SendRtpPacket(
        RTC::Consumer* consumer, RTC::RtpPacket* packet, onSendCallback* cb = nullptr) = 0;
      virtual void SendRtcpPacket(RTC::RTCP::Packet* packet)                 = 0;
      virtual void SendRtcpCompoundPacket(RTC::RTCP::CompoundPacket* packet) = 0;
    };
  public:
    Handler(Listener *l) : listener_(l) {}
    inline const ExtensionIds &ext_ids() const { return recvRtpHeaderExtensionIds; }
    inline ExtensionIds &ext_ids() { return recvRtpHeaderExtensionIds; }
    void OnTimer(qrpc_time_t now) { SendRtcp(qrpc_time_to_msec(now)); }
    bool SetExtensionId(uint8_t id, const std::string &uri);
  // borrow from src/ext/mediasoup/worker/include/RTC/Transport.hpp
  public:
    void ReceiveRtpPacket(const std::string &id, RTC::RtpPacket &packet);
    void ReceiveRtcpPacket(RTC::RTCP::Packet *packet);
  protected:
    void SendRtcp(uint64_t nowMs);
    void HandleRtcpPacket(RTC::RTCP::Packet *packet);
		inline void DataReceived(size_t len) { recvTransmission.Update(len, qrpc_time_msec(qrpc_time_now())); }
		inline void DataSent(size_t len) { sendTransmission.Update(len, qrpc_time_msec(qrpc_time_now())); }
  protected:
    Listener *listener_{ nullptr };
    ExtensionIds recvRtpHeaderExtensionIds;
		// Allocated by this.
		absl::flat_hash_map<std::string, RTC::Producer*> mapProducers;
		absl::flat_hash_map<std::string, RTC::Consumer*> mapConsumers;
		absl::flat_hash_map<uint32_t, RTC::Consumer*> mapSsrcConsumer;
		absl::flat_hash_map<uint32_t, RTC::Consumer*> mapRtxSsrcConsumer;
		std::shared_ptr<RTC::TransportCongestionControlClient> tccClient{ nullptr };
		std::shared_ptr<RTC::TransportCongestionControlServer> tccServer{ nullptr };
#ifdef ENABLE_RTC_SENDER_BANDWIDTH_ESTIMATOR
		std::shared_ptr<RTC::SenderBandwidthEstimator> senderBwe{ nullptr };
#endif
    // others
		RTC::RateCalculator recvTransmission;
		RTC::RateCalculator sendTransmission;
		RTC::RtpDataCounter recvRtpTransmission;
		RTC::RtpDataCounter sendRtpTransmission;
		RTC::RtpDataCounter recvRtxTransmission;
		RTC::RtpDataCounter sendRtxTransmission;
		RTC::RtpDataCounter sendProbationTransmission;
		uint16_t transportWideCcSeq{ 0u };
		uint32_t initialAvailableOutgoingBitrate{ 600000u };
		uint32_t maxIncomingBitrate{ 0u };
		uint32_t maxOutgoingBitrate{ 0u };
		uint32_t minOutgoingBitrate{ 0u };
  };
}
}