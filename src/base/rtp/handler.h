#pragma once

#include "base/defs.h"
#include "base/crypto.h"
#include "base/media.h"

#include "base/rtp/parameters.h"
#include "base/rtp/producer.h"
#include "base/rtp/shared.h"

#include "RTC/Consumer.hpp"
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
  class Handler : public RTC::Producer::Listener, public RTC::Consumer::Listener {
  public:
    typedef RTC::RtpHeaderExtensionIds ExtensionIds;
    class Listener {
    public:
      typedef const std::function<void(bool sent)> onSendCallback;  
    public:
      virtual const std::string &rtp_id() const = 0;
      virtual void RecvStreamClosed(uint32_t ssrc) = 0;
      virtual void SendStreamClosed(uint32_t ssrc) = 0;
      virtual bool IsConnected() const = 0;
      virtual void SendRtpPacket(
        RTC::Consumer* consumer, RTC::RtpPacket* packet, onSendCallback* cb = nullptr) = 0;
      virtual void SendRtcpPacket(RTC::RTCP::Packet* packet)                 = 0;
      virtual void SendRtcpCompoundPacket(RTC::RTCP::CompoundPacket* packet) = 0;
    };
  public:
    Handler(Listener &l) :
      listener_(l), producer_factory_(*this), shared_(),
      medias_(), rid_label_map_(), trackid_label_map_(), ssrc_trackid_map_(), rid_scalability_mode_map_() {}
    inline const ExtensionIds &ext_ids() const { return recvRtpHeaderExtensionIds; }
    inline ExtensionIds &ext_ids() { return recvRtpHeaderExtensionIds; }
    inline Listener &listener() { return listener_; }
    inline RTC::Shared &shared() { return shared_; }
    inline std::string FindScalabilityMode(const std::string &rid) {
      auto it = rid_scalability_mode_map_.find(rid);
      return it == rid_scalability_mode_map_.end() ? it->second : "";
    }
    qrpc_time_t OnTimer(qrpc_time_t now) { SendRtcp(qrpc_time_to_msec(now)); }
    bool SetExtensionId(uint8_t id, const std::string &uri);
    void SetNegotiationArgs(const std::map<std::string, json> &args);
    std::shared_ptr<Media> FindFrom(const Parameters &p);
    int CreateProducer(const std::string &id, const Parameters &p);
    int CreateConsumer(const Parameters &p);
    static inline qrpc_time_t RtcpRandomInterval() { return qrpc_time_msec(random::gen(1000, 1500)); }
		/* Pure virtual methods inherited from RTC::Producer::Listener. */
	public:
		void OnProducerReceiveData(RTC::Producer* /*producer*/, size_t len) override {
			this->DataReceived(len);
		}
		void OnProducerReceiveRtpPacket(RTC::Producer* /*producer*/, RTC::RtpPacket* packet) override {
			this->ReceiveRtpPacket(listener_.rtp_id(), *packet);
		}
		void OnProducerPaused(RTC::Producer* producer) override;
		void OnProducerResumed(RTC::Producer* producer) override;
		void OnProducerNewRtpStream(
		  RTC::Producer* producer, RTC::RtpStreamRecv* rtpStream, uint32_t mappedSsrc) override;
		void OnProducerRtpStreamScore(
		  RTC::Producer* producer,
		  RTC::RtpStreamRecv* rtpStream,
		  uint8_t score,
		  uint8_t previousScore) override;
		void OnProducerRtcpSenderReport(
		  RTC::Producer* producer, RTC::RtpStreamRecv* rtpStream, bool first) override;
		void OnProducerRtpPacketReceived(RTC::Producer* producer, RTC::RtpPacket* packet) override;
		void OnProducerSendRtcpPacket(RTC::Producer* producer, RTC::RTCP::Packet* packet) override;
		void OnProducerNeedWorstRemoteFractionLost(
		  RTC::Producer* producer, uint32_t mappedSsrc, uint8_t& worstRemoteFractionLost) override;

		/* Pure virtual methods inherited from RTC::Consumer::Listener. */
	public:
		void OnConsumerSendRtpPacket(RTC::Consumer* consumer, RTC::RtpPacket* packet) override;
		void OnConsumerRetransmitRtpPacket(RTC::Consumer* consumer, RTC::RtpPacket* packet) override;
		void OnConsumerKeyFrameRequested(RTC::Consumer* consumer, uint32_t mappedSsrc) override;
		void OnConsumerNeedBitrateChange(RTC::Consumer* consumer) override;
		void OnConsumerNeedZeroBitrate(RTC::Consumer* consumer) override;
		void OnConsumerProducerClosed(RTC::Consumer* consumer) override;    
  // borrow from src/ext/mediasoup/worker/include/RTC/Transport.hpp
  public:
    void ReceiveRtpPacket(const std::string &id, RTC::RtpPacket &packet);
    void ReceiveRtcpPacket(RTC::RTCP::Packet *packet);
		RTC::Consumer* GetConsumerByMediaSsrc(uint32_t ssrc) const;
		RTC::Consumer* GetConsumerByRtxSsrc(uint32_t ssrc) const;
  protected:
    void SendRtcp(uint64_t nowMs);
    void HandleRtcpPacket(RTC::RTCP::Packet *packet);
		inline void DataReceived(size_t len) { recvTransmission.Update(len, qrpc_time_msec(qrpc_time_now())); }
		inline void DataSent(size_t len) { sendTransmission.Update(len, qrpc_time_msec(qrpc_time_now())); }
  protected:
    Listener &listener_;
    ProducerFactory producer_factory_;
    Shared shared_;
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
  protected:
    std::map<Media::Id, std::shared_ptr<Media>> medias_;
    std::map<Media::Rid, Media::Id> rid_label_map_;
    std::map<Media::TrackId, Media::Id> trackid_label_map_;
    std::map<Media::Ssrc, Media::TrackId> ssrc_trackid_map_;
    std::map<Media::Rid, Media::ScalabilityMode> rid_scalability_mode_map_;
  };
}
}