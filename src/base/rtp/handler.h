#pragma once

#include "base/defs.h"
#include "base/crypto.h"
#include "base/media.h"

#include "base/rtp/consumer.h"
#include "base/rtp/parameters.h"
#include "base/rtp/producer.h"
#include "base/rtp/shared.h"

#include "RTC/RtpHeaderExtensionIds.hpp"
#include "RTC/RtpPacket.hpp"
#include "RTC/RateCalculator.hpp"
#ifdef ENABLE_RTC_SENDER_BANDWIDTH_ESTIMATOR
#include "RTC/SenderBandwidthEstimator.hpp"
#endif
#include "RTC/TransportCongestionControlClient.hpp"
#include "RTC/TransportCongestionControlServer.hpp"

#include <absl/container/flat_hash_map.h>

using json = nlohmann::json;

namespace base {
namespace rtp {
  class Handler :
    public RTC::Producer::Listener, public RTC::Consumer::Listener,
    public RTC::TransportCongestionControlClient::Listener,
    public RTC::TransportCongestionControlServer::Listener {
  public:
    typedef RTC::RtpHeaderExtensionIds ExtensionIds;
    struct Config {
      size_t initial_outgoing_bitrate;
      size_t max_outgoing_bitrate, max_incoming_bitrate;
      size_t min_outgoing_bitrate;
    };
    struct ConsumeOptions {
      bool pause_video{false};
      bool pause_audio{false};
    };
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
      virtual const Config &GetRtpConfig() const = 0;
    };
    typedef Listener::onSendCallback onSendCallback;
  public:
    Handler(Listener &l) :
      listener_(l), producer_factory_(*this), consumer_factory_(*this), medias_(),
      rid_label_map_(), trackid_label_map_(), ssrc_trackid_map_(), rid_scalability_mode_map_() {}
    inline const ExtensionIds &ext_ids() const { return recvRtpHeaderExtensionIds; }
    inline ExtensionIds &ext_ids() { return recvRtpHeaderExtensionIds; }
    inline Listener &listener() { return listener_; }
    inline const std::string &rtp_id() const { return listener_.rtp_id(); }
    inline RTC::Shared &shared() { return shared_.get(); }
    inline std::string FindScalabilityMode(const std::string &rid) {
      auto it = rid_scalability_mode_map_.find(rid);
      return it != rid_scalability_mode_map_.end() ? it->second : "";
    }
    qrpc_time_t OnTimer(qrpc_time_t now);
    bool Consume(
      Handler &peer, const std::string &media_id, const ConsumeOptions &options,
      std::vector<uint32_t> &generated_ssrcs
    );
    bool ConsumeMedia(
      Parameters::MediaKind kind, Handler &peer, const std::string &label, bool paused,
      std::vector<uint32_t> &generated_ssrcs
    );
    bool SetExtensionId(uint8_t id, const std::string &uri);
    void SetNegotiationArgs(const std::map<std::string, json> &args);
    std::shared_ptr<Media> FindFrom(const Parameters &p);
    std::shared_ptr<Media> FindFrom(const std::string &label);
    int Produce(const std::string &id, const Parameters &p);
    std::shared_ptr<Producer> FindProducer(const std::string &label, Parameters::MediaKind kind) const;
    void TransportConnected();
    void TransportDisconnected();
    static inline qrpc_time_t RtcpRandomInterval() { return qrpc_time_msec(random::gen(1000, 1500)); }
  public:
		/* implements RTC::Producer::Listener. */
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
  public:
		/* implements RTC::Consumer::Listener. */
		void OnConsumerSendRtpPacket(RTC::Consumer* consumer, RTC::RtpPacket* packet) override;
		void OnConsumerRetransmitRtpPacket(RTC::Consumer* consumer, RTC::RtpPacket* packet) override;
		void OnConsumerKeyFrameRequested(RTC::Consumer* consumer, uint32_t mappedSsrc) override;
		void OnConsumerNeedBitrateChange(RTC::Consumer* consumer) override;
		void OnConsumerNeedZeroBitrate(RTC::Consumer* consumer) override;
		void OnConsumerProducerClosed(RTC::Consumer* consumer) override;    
  public:
    /* implements RTC::TransportCongestionControlClient::Listener. */
    virtual void OnTransportCongestionControlClientBitrates(
      RTC::TransportCongestionControlClient* tccClient,
      RTC::TransportCongestionControlClient::Bitrates& bitrates) override;
    virtual void OnTransportCongestionControlClientSendRtpPacket(
      RTC::TransportCongestionControlClient* tccClient,
      RTC::RtpPacket* packet,
      const webrtc::PacedPacketInfo& pacingInfo) override;
  public:
    /* implements RTC::TransportCongestionControlServer::Listener. */
    void OnTransportCongestionControlServerSendRtcpPacket(
			  RTC::TransportCongestionControlServer* tccServer, RTC::RTCP::Packet* packet) override;

  public:
    // from src/ext/mediasoup/worker/include/RTC/Transport.hpp
    void ReceiveRtpPacket(const std::string &id, RTC::RtpPacket &packet);
    void ReceiveRtcpPacket(RTC::RTCP::Packet *packet);
		RTC::Consumer* GetConsumerByMediaSsrc(uint32_t ssrc) const;
		RTC::Consumer* GetConsumerByRtxSsrc(uint32_t ssrc) const;
		inline void DataReceived(size_t len) { recvTransmission.Update(len, qrpc_time_msec(qrpc_time_now())); }
		inline void DataSent(size_t len) { sendTransmission.Update(len, qrpc_time_msec(qrpc_time_now())); }
  protected:
    void SendRtcp(uint64_t nowMs);
    void HandleRtcpPacket(RTC::RTCP::Packet *packet);
    void DistributeAvailableOutgoingBitrate();
    void ComputeOutgoingDesiredBitrate(bool forceBitrate = false);
		void EmitTraceEventProbationType(RTC::RtpPacket* packet) const;
		void EmitTraceEventBweType(RTC::TransportCongestionControlClient::Bitrates& bitrates) const;
  protected:
    int CreateConsumer(const Parameters &p);
    void InitTccClient(Consumer *consumer, const Parameters &p);
    void InitTccServer(const Parameters &p);
  protected:
    Listener &listener_;
    static thread_local Shared shared_;
    ProducerFactory producer_factory_; // should be declared prior to Producer* and Consumer* containers
    ConsumerFactory consumer_factory_;
    ExtensionIds recvRtpHeaderExtensionIds;
		// Allocated by this.
		// absl::flat_hash_map<std::string, RTC::Producer*> mapProducers;
		absl::flat_hash_map<RTC::Producer*, absl::flat_hash_set<RTC::Consumer*>> mapProducerConsumers;
		absl::flat_hash_map<RTC::Consumer*, RTC::Producer*> mapConsumerProducer;
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
    struct TraceEventTypes {
			bool probation{ false };
			bool bwe{ false };
		} traceEventTypes;
  protected:
    std::map<Media::Id, std::shared_ptr<Media>> medias_;
    std::map<Media::Rid, Media::Id> rid_label_map_;
    std::map<Media::TrackId, Media::Id> trackid_label_map_;
    std::map<Media::Mid, Media::Id> mid_label_map_;
    std::map<Media::Ssrc, Media::TrackId> ssrc_trackid_map_;
    std::map<Media::Rid, Media::ScalabilityMode> rid_scalability_mode_map_;
  };
}
}