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
    typedef ::flatbuffers::FlatBufferBuilder FBB;
    struct Config {
      size_t initial_outgoing_bitrate;
      size_t max_outgoing_bitrate, max_incoming_bitrate;
      size_t min_outgoing_bitrate;
    };
    struct MediaStreamConfig : public Parameters {
      struct ControlOptions {
        ControlOptions(const json &j);
        ControlOptions() : pause(false) {}
        bool pause;
      };
      enum Direction { SEND, RECV };
      MediaStreamConfig() : Parameters() {}
      MediaStreamConfig(const Parameters &p, Direction d) : Parameters(p), direction(d) {}
      inline bool sender() const { return direction == Direction::SEND; }
      inline bool receiver() const { return direction == Direction::RECV; }
      inline bool probator() const { return mid == RTC::RtpProbationGenerator::GetMidValue(); }
      inline const std::string media_stream_track_id() const {
        return probator() ? mid : media_path;
      }
      inline const std::string media_stream_id() const {
        if (probator()) { return mid; }
        auto cs = str::Split(media_path, "/");
        if (cs.size() == 3) {
          return cs[0] + "/" + cs[1];
        } else if (cs.size() == 2) {
          return cs[0];
        } else {
          ASSERT(false);
          return "";
        }
      }
      bool GenerateCN(std::string &cname) const;
      std::string media_path;
      Direction direction{ Direction::RECV };
      ControlOptions options;
    };
    typedef std::vector<MediaStreamConfig> MediaStreamConfigs;
    struct RouterListener : RTC::Router::Listener {
      RTC::WebRtcServer* OnRouterNeedWebRtcServer(
			  RTC::Router* router, std::string& webRtcServerId) override { return nullptr; }
    };
    class Listener {
    public:
      typedef const std::function<void(bool sent)> onSendCallback;  
    public:
      virtual const std::string &rtp_id() const = 0;
      virtual const std::string &cname() const = 0;
      virtual const std::map<Parameters::MediaKind, Capability> &capabilities() const = 0;
      virtual const std::string &FindRtpIdFrom(std::string &cname) = 0;
      virtual const std::string GenerateMid() = 0;
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
    Handler(Listener &l) : RTC::Transport(&shared(), l.rtp_id(), &router(), TransportOptions(l.GetRtpConfig())),
      listener_(l), producer_factory_(*this), consumer_factory_(*this), medias_(),
      rid_label_map_(), trackid_label_map_(), ssrc_trackid_map_(), rid_scalability_mode_map_() {}
    inline Listener &listener() { return listener_; }
    inline const std::string &rtp_id() const { return listener_.rtp_id(); }
    inline const std::string &cname() const { return listener_.cname(); }
    inline const std::map<Media::Mid, Media::Id> mid_media_path_map() const { return mid_media_path_map_; }
    static inline RTC::Shared &shared() { return shared_.get(); }
    static inline RTC::Router &router() { return router_; }
    static inline Channel::ChannelSocket &socket() { return shared_.socket(); }
    static FBB &GetFBB() {
      static thread_local ::flatbuffers::FlatBufferBuilder fbb;
      fbb.Clear();
      return fbb;
    }
    static void ConfigureLogging(const std::string &log_level, const std::vector<std::string> &log_tags);
    static const FBS::Transport::Options* TransportOptions(const Config &c);
    static const std::vector<Parameters::MediaKind> &SupportedMediaKind();
    inline const std::string &FindScalabilityMode(const std::string &rid) const {
      auto it = rid_scalability_mode_map_.find(rid);
      return it != rid_scalability_mode_map_.end() ? it->second : "";
    }
    inline std::string GenerateMid() { return listener_.GenerateMid(); }
    qrpc_time_t OnTimer(qrpc_time_t now);
    template <typename Body>
    static Channel::ChannelRequest CreateRequest(FBB &fbb, FBS::Request::Method m, ::flatbuffers::Offset<Body> ofs = 0) {
      auto btit = payload_map_.find(m);
      if (btit == payload_map_.end()) {
        ASSERT(ofs.o == 0);
        fbb.Finish(FBS::Request::CreateRequestDirect(fbb, 0, m, "dummy"));
      } else {
        fbb.Finish(FBS::Request::CreateRequestDirect(
          fbb, 0, m, "dummy", btit->second, ::flatbuffers::Offset<void>(ofs.o)
        ));
      }
      return Channel::ChannelRequest(&socket(), ::flatbuffers::GetRoot<FBS::Request::Request>(fbb.GetBufferPointer()));
    }
    template <typename Body> void HandleRequest(FBB &fbb, FBS::Request::Method m, ::flatbuffers::Offset<Body> ofs) { 
      RTC::Transport::HandleRequest(&Handler::CreateRequest(fbb, m, ofs));
    }
    void Close();
    int Produce(const std::string &id, const Parameters &p);
    bool PrepareConsume(
      Handler &peer, const std::vector<std::string> &parsed_media_path, 
      const std::map<rtp::Parameters::MediaKind, MediaStreamConfig::ControlOptions> options_map,
      MediaStreamConfigs &consume_configs, std::vector<uint32_t> &generated_ssrcs);
    bool Consume(Handler &peer, const std::string &label, const MediaStreamConfig &config);
    bool ControlStream(const std::string &label, const std::string &control, std::string &error);
    bool Pause(const std::string &label, std::string &error) { return ControlStream(label, "pause", error); }
    bool Resume(const std::string &label, std::string &error) { return ControlStream(label, "resume", error); }
    bool SetExtensionId(uint8_t id, RTC::RtpHeaderExtensionUri::Type uri);
    void SetNegotiationArgs(const std::map<std::string, json> &args);
    void UpdateMidMediaPathMap(const std::string &mid, const std::string &path) {
      mid_media_path_map_[mid] = path;
			QRPC_LOGJ(info, {{"ev","new mid label map"},{"map",mid_media_path_map_}});
    }
    void UpdateByCapability(const Capability &cap);
    std::string FindLabelByMid(const std::string &mid) const {
      auto it = mid_media_path_map_.find(mid);
      if (it == mid_media_path_map_.end()) {
        return "";
      }
      auto label_kind = str::Split(it->second, "/");
      if (label_kind.size() == 3) {
        return label_kind[1];
      } else if (label_kind.size() == 2) {
        return label_kind[0];
      } else {
        QRPC_LOGJ(error, {{"ev","invalid label_kind"},{"label_kind",label_kind}});
        ASSERT(false);
        return "";
      }
    }
    std::shared_ptr<Media> FindFrom(const Parameters &p);
    std::shared_ptr<Media> FindFrom(const std::string &label);
    Producer *FindProducer(const std::string &label, Parameters::MediaKind kind) const;
    inline Producer *FindProducer(const std::string& producerId) const {
      try { return reinterpret_cast<Producer *>(GetProducerById(producerId)); }
      catch (std::exception &) { return nullptr; }
    }
    inline Consumer *FindConsumer(const std::string &consumerId) const {
      try { return reinterpret_cast<Consumer *>(GetConsumerById(consumerId)); }
      catch (std::exception &) { return nullptr; }
    }
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
    std::map<Media::Mid, std::string> mid_media_path_map_;
    std::map<Media::Ssrc, Media::TrackId> ssrc_trackid_map_;
    std::map<Media::Rid, Media::ScalabilityMode> rid_scalability_mode_map_;
  };
}
}