#pragma once

#include "base/defs.h"
#include "base/crypto.h"
#include "base/media.h"
#include "base/stream.h"

#include "base/rtp/consumer.h"
#include "base/rtp/parameters.h"
#include "base/rtp/producer.h"
#include "base/rtp/router.h"
#include "base/rtp/shared.h"

#include "RTC/RtpHeaderExtensionIds.hpp"
#include "RTC/RtpPacket.hpp"
#include "RTC/RateCalculator.hpp"
#ifdef ENABLE_RTC_SENDER_BANDWIDTH_ESTIMATOR
#include "RTC/SenderBandwidthEstimator.hpp"
#endif
#include "RTC/Transport.hpp"
#include "RTC/TransportCongestionControlClient.hpp"
#include "RTC/TransportCongestionControlServer.hpp"

#include <absl/container/flat_hash_map.h>

using json = nlohmann::json;

namespace base {
namespace rtp {
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
    inline bool closed() const { return this->close_flag != 0; }
    inline const std::string media_stream_track_id() const {
      return probator() ? mid : media_path;
    }
    inline const std::string local_path() const {
      if (sender()) {
        auto idx = media_path.find("/");
        ASSERT(idx != std::string::npos);
        return media_path.substr(idx + 1);
      } else  {
        ASSERT(receiver());
        return media_path;
      }
    }
    inline const std::string media_stream_id() const {
      if (probator()) { return mid; }
      auto cs = str::Split(media_path, "/");
      if (cs.size() <= 1) {
        ASSERT(false);
        return "";
      }
      cs[cs.size() - 1] = std::to_string(this->reuse_count);
      return str::Join(cs, "/");
    }
    bool GenerateCN(std::string &cname) const;
    void Close() { this->close_flag = 1; }
    void Reuse() { this->reuse_count++; } // ok to reset to 0
    void Reconnect() { this->reconnect_count++; }
    void Reset() {
      auto new_seed = GenerateSsrc();
      QRPC_LOGJ(info, {{"ev","reset config"},{"mld",mid},{"path",media_path},{"new_seed",new_seed}});
      this->ssrc_seed = new_seed;
      this->close_flag = 0;
      this->encodings.clear();
      this->codecs.clear();
      this->headerExtensions.clear();
      this->ssrcs.clear();
    }
    std::string media_path;
    Direction direction{ Direction::RECV };
    ControlOptions options;
    std::map<uint32_t, std::string> related_ssrcs; // for producer, previously related ssrcs and rid (if any)
    uint8_t close_flag{0}, reuse_count{0}, reconnect_count{0};
  };
  struct RemoteAnswer {
    RemoteAnswer() : ssrc_fixups() {}
    RemoteAnswer(const json &j) : ssrc_fixups() {
      auto it = j.find("ssrc_fixups");
      if (it != j.end()) {
        bool processed = false;
        for (auto &pair : *it) {
          QRPC_LOGJ(info, {{"ev","ssrc fixup pair"},{"pair",pair}});
          auto from = pair[0].get<uint32_t>();
          auto to = pair[1].get<uint32_t>();
          ssrc_fixups[from] = to;
          processed = true;
        }
        ASSERT(processed);
      } else {
        QRPC_LOGJ(warn, {{"ev","no ssrc fixup found"},{"src",j}});
        ASSERT(false);
      }
    }
    std::map<uint32_t, uint32_t> ssrc_fixups;
  };
  struct StreamRecoveryContext {
    std::string rid, mid;
    uint32_t rtp_roc; // rollover counter for recovered rtp stream, to work srtp decryption works correctly
    bool try_rid_complement{false};
  };
  class MediaStreamConfigs : public std::vector<MediaStreamConfig> {
  public:
    inline MediaStreamConfig &NewSlot(Parameters::MediaKind kind) {
      for (auto &c : *this) {
        if (c.closed() && c.kind == kind) {
          // can reuse closed slot
          c.close_flag = 0;
          c.encodings.clear();
          c.codecs.clear();
          c.headerExtensions.clear();
          c.ssrcs.clear();
          return c;
        }
      }
      return this->emplace_back();
    }
    inline MediaStreamConfig *FindSlot(const std::string &mid) {
      for (auto &c : *this) {
        if (c.mid == mid) {
          return &c;
        }
      }
      return nullptr;
    }
  };
  class Handler : public RTC::Transport {
  public:
    typedef RTC::RtpHeaderExtensionIds ExtensionIds;
    typedef ::flatbuffers::FlatBufferBuilder FBB;
    struct Config {
      size_t initial_outgoing_bitrate;
      size_t max_outgoing_bitrate, max_incoming_bitrate;
      size_t min_outgoing_bitrate;
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
      virtual const std::string &cname() const = 0;
      virtual const std::map<Parameters::MediaKind, Capability> &capabilities() const = 0;
      virtual MediaStreamConfigs &media_stream_configs() = 0;
      virtual const std::string &FindRtpIdFrom(std::string &cname) = 0;
      virtual const std::string GenerateMid() = 0;
      virtual int SendToStream(const std::string &label, const char *data, size_t len) = 0;
      // for RTC::Transport::Listener
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
      virtual bool GetRtpRoc(uint32_t ssrc, uint32_t &roc, MediaStreamConfig::Direction dir) = 0;
      virtual const Config &GetRtpConfig() const = 0;
    };
    typedef Listener::onSendCallback onSendCallback;
  public:
    Handler(Listener &l) : RTC::Transport(&shared(), l.rtp_id(), &router(), TransportOptions(l.GetRtpConfig())),
      listener_(l), producer_factory_(*this), consumer_factory_(*this), medias_(), mid_media_path_map_() {}
    ~Handler() override { RTC::Transport::CloseProducersAndConsumers(); }
    inline Listener &listener() { return listener_; }
    inline const std::string &rtp_id() const { return listener_.rtp_id(); }
    inline const std::string &cname() const { return listener_.cname(); }
    inline const absl::flat_hash_map<std::string, RTC::Consumer*> &consumers() const { return this->mapConsumers; }
    inline const absl::flat_hash_map<std::string, RTC::Producer*> &producers() const { return this->mapProducers; }
    inline const std::map<Media::Mid, Media::Id> mid_media_path_map() const { return mid_media_path_map_; }
    inline std::map<uint32_t, StreamRecoveryContext> &ssrc_stream_recovery_map() { return ssrc_stream_recovery_map_; }
    inline int SendToStream(const std::string &path, const char *data, size_t len) {
      return listener_.SendToStream(path, data, len);
    }
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
    Producer *Produce(const MediaStreamConfig &p);
    bool ApplyAnswer(const std::string &mid, const RemoteAnswer &answer, std::string &error);
    bool PrepareConsume(
      Handler &peer, const std::string &local_path, const std::optional<Parameters::MediaKind> &media_kind,
      const std::map<Parameters::MediaKind, MediaStreamConfig::ControlOptions> options_map, bool sync,
      MediaStreamConfigs &consume_configs, std::map<std::string,Consumer*> &created_consumers);
    bool CloseMedia(
      const std::string &local_path, const std::optional<Parameters::MediaKind> &media_kind,
      MediaStreamConfigs &media_stream_configs, std::vector<std::string> &closed_paths, std::string &error);
    bool Consume(Handler &peer, const MediaStreamConfig &config, std::string &error);
    bool ControlStream(const std::string &path, const std::string &control, bool &is_producer, std::string &error);
    bool CloseStream(MediaStreamConfig &config, std::string &error);
    bool Pause(const std::string &path, std::string &error);
    bool Resume(const std::string &path, std::string &error);
    bool Ping(std::string &error);
    bool SetExtensionId(uint8_t id, RTC::RtpHeaderExtensionUri::Type uri);
    void UpdateMidMediaPathMap(const MediaStreamConfig &c) {
      mid_media_path_map_[c.mid] = c.media_path;
			QRPC_LOGJ(info, {{"ev","new mid label map"},{"map",mid_media_path_map_}});
    }
    void UpdateByCapability(const Capability &cap);
    std::shared_ptr<Media> FindFrom(const Parameters &p, bool consumer);
    std::shared_ptr<Media> FindFrom(const std::string &label, bool consumer);
    inline Producer *FindProducerByPath(const std::string &path) const {
      return FindProducer(ProducerFactory::GenerateId(rtp_id(), path));
    }
    inline Producer *FindProducer(const std::string& producerId) const {
      auto it = this->mapProducers.find(producerId);
      return reinterpret_cast<Producer *>(it == this->mapProducers.end() ? nullptr : it->second);
    }
    inline Consumer *FindConsumerByPath(const std::string &path) const {
      return FindConsumer(ConsumerFactory::GenerateId(rtp_id(), path));
    }
    inline Consumer *FindConsumer(const std::string &consumerId) const {
      auto it = this->mapConsumers.find(consumerId);
      return reinterpret_cast<Consumer *>(it == this->mapConsumers.end() ? nullptr : it->second);
    }
    void SendToConsumersOf(
      const RTC::Producer *p, const std::string &stream_label, const char *data, size_t len
    );
    void SendToConsumersOf(
      const std::string &path, const std::string &stream_label, const char *data, size_t len
    ) {
      auto p = GetProducerById(ProducerFactory::GenerateId(rtp_id(), path));
      SendToConsumersOf(p, stream_label, data, len);
    }
    void CloseConsumer(Consumer *c);
    void CloseProducer(Producer *p);
    void TryRidComplement(uint32_t ssrc);
    void FixListnerSsrcMap(Producer *p, uint32_t old_ssrc, uint32_t new_ssrc);
    void DumpChildren(); // dump consumer and producer created by this handler
  public:
    void ReceiveRtpPacket(RTC::RtpPacket* packet);
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
  public:
    void PublishStream(const std::shared_ptr<base::Stream> &stream);
    void UnpublishStream(const std::shared_ptr<base::Stream> &stream);
    void EmitSubscribeStreams(const std::shared_ptr<base::Stream> &stream, const void *p, size_t sz);
    bool SubscribeStream(const std::string &path, const std::shared_ptr<base::Stream> &stream);
    void UnsubscribeStream(const Stream *stream); // this need to be called method of Stream object itself
  protected:
    static thread_local Shared shared_;
    static thread_local Router router_;
    static const std::map<FBS::Request::Method, FBS::Request::Body> payload_map_;
    Listener &listener_;
    ProducerFactory producer_factory_; // should be declared prior to Producer* and Consumer* containers
    ConsumerFactory consumer_factory_;
    std::map<std::string, std::shared_ptr<base::Stream>> published_streams_;
    std::map<Media::Id, std::shared_ptr<Media>> medias_;
    std::map<Media::Mid, std::string> mid_media_path_map_;
    std::map<uint32_t, StreamRecoveryContext> ssrc_stream_recovery_map_;
  };
}
}