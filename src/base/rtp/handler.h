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
#include "RTC/Router.hpp"
#include "RTC/Transport.hpp"
#include "RTC/TransportCongestionControlClient.hpp"
#include "RTC/TransportCongestionControlServer.hpp"

#include <absl/container/flat_hash_map.h>

using json = nlohmann::json;

namespace base {
namespace rtp {
  class Handler : public RTC::Transport {
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
    struct RouterListener : RTC::Router::Listener {
      RTC::WebRtcServer* OnRouterNeedWebRtcServer(
			  RTC::Router* router, std::string& webRtcServerId) override { return nullptr; }
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
      virtual void SendMessage(
        RTC::DataConsumer* dataConsumer,
        const uint8_t* msg,
        size_t len,
        uint32_t ppid,
        onQueuedCallback* = nullptr)                             = 0;
      virtual void SendSctpData(const uint8_t* data, size_t len) = 0;
      virtual const Config &GetRtpConfig() const = 0;
    };
    typedef Listener::onSendCallback onSendCallback;
  public:
    Handler(Listener &l) : RTC::Transport(&shared(), l.rtp_id(), &router(), TransportOptions()),
      listener_(l), producer_factory_(*this), consumer_factory_(*this), medias_(),
      rid_label_map_(), trackid_label_map_(), ssrc_trackid_map_(), rid_scalability_mode_map_() {}
    inline Listener &listener() { return listener_; }
    inline const std::string &rtp_id() const { return listener_.rtp_id(); }
    static inline RTC::Shared &shared() { return shared_.get(); }
    static inline RTC::Router &router() { return router_; }
    static inline Channel::ChannelSocket &socket() { return shared_.socket(); }
    static ::flatbuffers::FlatBufferBuilder &GetFBB() {
      static thread_local ::flatbuffers::FlatBufferBuilder fbb;
      fbb.Clear();
      return fbb;
    }
    static const FBS::Transport::Options* TransportOptions();
    inline std::string FindScalabilityMode(const std::string &rid) {
      auto it = rid_scalability_mode_map_.find(rid);
      return it != rid_scalability_mode_map_.end() ? it->second : "";
    }
    qrpc_time_t OnTimer(qrpc_time_t now);
    template <typename BodyOffset> void HandleRequest(FBS::Request::Method m, BodyOffset ofs) {
      auto btit = payload_map_.find(m);
      if (btit == payload_map_.end()) {
        ASSERT(false);
        return;
      }
      auto &fbb = GetFBB();
      fbb.Finish(FBS::Request::CreateRequestDirect(
        fbb, 0, m, nullptr, btit->second, flatbuffers::Offset<void>(ofs.o)
      ));
      RTC::Transport::HandleRequest(
        &Channel::ChannelRequest(&socket(), flatbuffers::GetRoot<FBS::Request::Request>(fbb.GetBufferPointer()))
      );
    }
    int Produce(const std::string &id, const Parameters &p);
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
    Producer *FindProducer(const std::string &label, Parameters::MediaKind kind) const;
    inline Producer *FindProducer(const std::string& producerId) const { return reinterpret_cast<Producer *>(GetProducerById(producerId)); }
    inline Consumer *FindConsumer(const std::string &consumerId) const { return reinterpret_cast<Consumer *>(GetConsumerById(consumerId)); }
  public:
    void ReceiveRtpPacket(RTC::RtpPacket* packet) { RTC::Transport::ReceiveRtpPacket(packet); }
    void ReceiveRtcpPacket(RTC::RTCP::Packet* packet) { RTC::Transport::ReceiveRtcpPacket(packet); }
    void DataSent(size_t len) { RTC::Transport::DataSent(len); }
    void Connected() { RTC::Transport::Connected(); }
    void Disconnected() { RTC::Transport::Disconnected(); }
  public:
    // implements RTC::Transport
    void RecvStreamClosed(uint32_t ssrc) override { listener_.RecvStreamClosed(ssrc); }
    void SendStreamClosed(uint32_t ssrc) override { listener_.SendStreamClosed(ssrc); }
    bool IsConnected() const override { return listener_.IsConnected(); }
    void SendRtpPacket(
      RTC::Consumer* consumer, RTC::RtpPacket* packet, onSendCallback* cb = nullptr) override {
      listener_.SendRtpPacket(consumer, packet, cb);
    }
    void SendRtcpPacket(RTC::RTCP::Packet* packet) override { listener_.SendRtcpPacket(packet); }
    void SendRtcpCompoundPacket(RTC::RTCP::CompoundPacket* packet) override { listener_.SendRtcpCompoundPacket(packet); }
    void SendMessage(
      RTC::DataConsumer* dataConsumer,
      const uint8_t* msg,
      size_t len,
      uint32_t ppid,
      onQueuedCallback *cb = nullptr) override { listener_.SendMessage(dataConsumer, msg, len, ppid, cb); }
    void SendSctpData(const uint8_t* data, size_t len) override { listener_.SendSctpData(data, len); }
  protected:
    static thread_local Shared shared_;
    static thread_local RTC::Router router_;
    static thread_local const std::map<FBS::Request::Method, FBS::Request::Body> payload_map_;
    Listener &listener_;
    ProducerFactory producer_factory_; // should be declared prior to Producer* and Consumer* containers
    ConsumerFactory consumer_factory_;
    std::map<Media::Id, std::shared_ptr<Media>> medias_;
    std::map<Media::Rid, Media::Id> rid_label_map_;
    std::map<Media::TrackId, Media::Id> trackid_label_map_;
    std::map<Media::Mid, Media::Id> mid_label_map_;
    std::map<Media::Ssrc, Media::TrackId> ssrc_trackid_map_;
    std::map<Media::Rid, Media::ScalabilityMode> rid_scalability_mode_map_;
  };
}
}