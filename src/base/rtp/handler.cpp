#include "base/rtp/handler.h"
#include "base/webrtc/mpatch.h"
#include "base/logger.h"
#include "base/string.h"

#include "base/webrtc/mpatch.h"
#include "base/rtp/parameters.h"

#include <flatbuffers/idl.h>

#include "RTC/BweType.hpp"
// #include "RTC/PipeConsumer.hpp"
#include "RTC/RTCP/FeedbackPs.hpp"
#include "RTC/RTCP/FeedbackPsAfb.hpp"
#include "RTC/RTCP/FeedbackPsRemb.hpp"
#include "RTC/RTCP/FeedbackRtp.hpp"
#include "RTC/RTCP/FeedbackRtpNack.hpp"
#include "RTC/RTCP/FeedbackRtpTransport.hpp"
#include "RTC/RTCP/XrDelaySinceLastRr.hpp"
#include "RTC/RtpDictionaries.hpp"
// #include "RTC/SimpleConsumer.hpp"
// #include "RTC/SimulcastConsumer.hpp"
// #include "RTC/SvcConsumer.hpp"
#include "RTC/RtcLogger.hpp"
#include "RTC/TransportCongestionControlClient.hpp"
#include "RTC/TransportCongestionControlServer.hpp"
#include <libwebrtc/modules/rtp_rtcp/include/rtp_rtcp_defines.h> // webrtc::RtpPacketSendInfo

#include <thread>

namespace base {
namespace rtp {
	thread_local std::map<std::string, std::string> schemas_;
	static const std::string &InitSchema(const std::string &type) {
		auto it = schemas_.find(type);
		if (it != schemas_.end()) {
			return it->second;
		}
		std::string path = "/Users/iyatomi/Documents/suntomi/qrpc/src/ext/mediasoup/worker/fbs/" + type + ".fbs";
		std::string content;
		if (!::flatbuffers::LoadFile(path.c_str(), false, &content)) {
			logger::die({{"ev","fail to load file"},{"path",path}});
		}
		schemas_[type] = content;
		return schemas_.find(type)->second;
	}
	template <class T>
	static std::string Dump(const std::string &type, const std::string &table, ::flatbuffers::FlatBufferBuilder &fbb) {
		std::string ret;
		static const char *paths[] = {
			"/Users/iyatomi/Documents/suntomi/qrpc/src/ext/mediasoup/worker/fbs/"
		};
		::flatbuffers::Parser parser;
		auto &s = InitSchema(type);
		if (!parser.Parse(s.c_str(), paths)) {
			logger::die({{"ev","fail to parse schema"},{"type",type},{"error",parser.error_}});
		}
		auto r = ::flatbuffers::GenTextFromTable(parser, ::flatbuffers::GetRoot<T>(fbb.GetBufferPointer()), table, &ret);
		ASSERT(!r); // non null means failure
		return ret;
	}
	static std::string to_string(std::thread::id id) {
    std::stringstream ss;
    ss << id;
    return ss.str();
  }
  thread_local Shared Handler::shared_;
	thread_local Handler::RouterListener router_listener_;
	thread_local RTC::Router Handler::router_(&shared_.get(), to_string(std::this_thread::get_id()), &router_listener_);
	thread_local const std::map<FBS::Request::Method, FBS::Request::Body> Handler::payload_map_ = {
		{ FBS::Request::Method::TRANSPORT_CONSUME, FBS::Request::Body::Transport_ConsumeRequest },
		{ FBS::Request::Method::TRANSPORT_PRODUCE, FBS::Request::Body::Transport_ProduceRequest },
		// more to come if needed
	};

	Handler::ConsumeOptions::ConsumeOptions(const json &j) {
		if (j.is_object()) {
			if (j.contains("pause")) {
				pause = j["pause"].get<bool>();
			}
		}
	}
	
	const FBS::Transport::Options* Handler::TransportOptions(const Config &c) {
		auto &fbb = GetFBB();
		fbb.Finish(FBS::Transport::CreateOptions(
			fbb, false, std::nullopt, c.initial_outgoing_bitrate, false)
		);
		return ::flatbuffers::GetRoot<FBS::Transport::Options>(fbb.GetBufferPointer());
	}
	bool Handler::PrepareConsume(
      Handler &peer, const std::vector<std::string> &parsed_media_path,
			const std::map<rtp::Parameters::MediaKind, ConsumeOptions> options_map,
			std::map<std::string, rtp::Handler::ConsumeConfig> &consume_config_map,
			std::vector<uint32_t> &generated_ssrcs
	) {
		static const std::vector<Parameters::MediaKind> kinds = {
			Parameters::MediaKind::AUDIO, Parameters::MediaKind::VIDEO
		};
		const auto &label = parsed_media_path[1];
		for (const auto k : kinds) {
			auto media_path = parsed_media_path[0] + "/" + label + "/" + Parameters::FromMediaKind(k);
			if (consume_config_map.find(media_path) != consume_config_map.end()) {
				QRPC_LOGJ(info, {
					{"ev","ignore media because already prepared"},
					{"label",label},{"kind",Parameters::FromMediaKind(k)}
				});
				continue;
			}
			auto local_producer = FindProducer(label, k);
			if (local_producer == nullptr) {
				QRPC_LOGJ(info, {
					{"ev","ignore media because corresponding producer not found"},
					{"label",label},{"kind",Parameters::FromMediaKind(k)}
				});
				continue;
			}
			auto consumed_producer = peer.FindProducer(label, k);
			if (local_producer == nullptr) {
				QRPC_LOGJ(info, {
					{"ev","ignore media because corresponding producer of peer not found"},
					{"label",label},{"kind",Parameters::FromMediaKind(k)},{"peer",peer.rtp_id()}
				});
				continue;
			}
			auto entry = consume_config_map.emplace(media_path, ConsumeConfig());
			auto &config = entry.first->second;
			config.media_path = media_path;
			config.options = options_map.find(k) == options_map.end() ? ConsumeOptions() : options_map.find(k)->second;
			// generate unique mid from own consumer factory
			auto mid = consumer_factory_.GenerateMid();
			config.mid = mid;
			// copy additional parameter from producer that affects sdp generation
			config.kind = k;
			config.network = consumed_producer->params().network;
			config.rtp_proto = consumed_producer->params().rtp_proto;
			config.ssrc_seed = consumed_producer->params().ssrc_seed;
			// generate rtp parameter from this handler_'s capabality (of corresponding producer) and consumed_producer's encodings
			if (!local_producer->consumer_params(consumed_producer->params(), config)) {
				QRPC_LOGJ(error, {{"ev","fail to generate cosuming params"}});
				ASSERT(false);
				continue;
			}
			config.rtcp.cname = peer.cname();
			config.GetGeneratedSsrc(generated_ssrcs);
			// if video consumer and there is no probator mid, generate probator mid => param pair
			auto probator_mid = RTC::RtpProbationGenerator::GetMidValue();
			if (k == Parameters::MediaKind::VIDEO && consume_config_map.find(probator_mid) == consume_config_map.end()) {
				consume_config_map.emplace(probator_mid, ConsumeConfig(config.ToProbator()));
			}
		}
		QRPC_LOGJ(info, {{"ev","set consume configs"},{"configs",consume_config_map.size()}});
		return true;
	}

	bool Handler::Consume(
		Handler &peer, const std::string &label, const ConsumeConfig &config
	) {
		auto cid = ConsumerFactory::GenerateId(peer.rtp_id(), label, config.kind);
		if (HasConsumer(cid)) {
			QRPC_LOGJ(info, {{"ev","consume already created"},{"cid",cid}});
			return true;
		}
		const auto &options = config.options;
		const auto &kind = config.kind;
		// false for creating pipe consumer: TODO: support pipe consumer
		auto consumer = consumer_factory_.Create(peer, label, kind, config, options.pause, false);
		if (consumer == nullptr) {
				QRPC_LOGJ(error, {{"ev","fail to create consumer"}});
				ASSERT(false);
				return false;
		}
		// auto &fbb = GetFBB();
		// fbb.Finish(consumer_factory_.FillBuffer(consumer, fbb));
		// puts(Dump<FBS::Consumer::DumpResponse>("consumer", "FBS.Consumer.DumpResponse", fbb).c_str());
		return true;
	}
	Producer *Handler::FindProducer(const std::string &label, Parameters::MediaKind kind) const {
		auto gid = ProducerFactory::GenerateId(rtp_id(), label, kind);
		return FindProducer(gid);
	}
	void Handler::SetNegotiationArgs(const std::map<std::string, json> &args) {
		const auto rlmit = args.find("ridLabelMap");
		if (rlmit != args.end()) {
			rid_label_map_ = rlmit->second.get<std::map<Media::Rid,Media::Id>>();
			QRPC_LOGJ(info, {{"ev","new rid label map"},{"map",*rlmit}});
		}
		const auto tlmit = args.find("trackIdLabelMap");
		if (tlmit != args.end()) {
			trackid_label_map_ = tlmit->second.get<std::map<Media::TrackId,Media::Id>>();
			QRPC_LOGJ(info, {{"ev","new track id label map"},{"map",*tlmit}});
		}
		const auto rsmit = args.find("ridScalabilityModeMap");
		if (rsmit != args.end()) {
			rid_scalability_mode_map_ = rsmit->second.get<std::map<Media::Rid,Media::ScalabilityMode>>();
			QRPC_LOGJ(info, {{"ev","new rid scalability mode map"},{"map",*rsmit}});
		}
		const auto midit = args.find("midLabelMap");
		if (midit != args.end()) {
			mid_label_map_ = midit->second.get<std::map<Media::Mid,Media::Id>>();
			QRPC_LOGJ(info, {{"ev","new mid label map"},{"map",*midit}});
		}
	}
	std::shared_ptr<Media> Handler::FindFrom(const std::string &label) {
		auto mit = medias_.find(label);
		if (mit == medias_.end()) {
			auto m = std::make_shared<Media>(label);
			medias_[label] = m;
			return m;
		}
		return mit->second;
	}
	std::shared_ptr<Media> Handler::FindFrom(const Parameters &p) {
		// 1. if encodings[*].rid has value, find label from rid by using rid_label_map_
		for (auto &e : p.encodings) {
			if (e.rid.empty()) {
				continue;
			}
			auto lit = rid_label_map_.find(e.rid);
			if (lit != rid_label_map_.end()) {
				auto &label = lit->second;
				auto mit = medias_.find(label);
				if (mit != medias_.end()) {
					return mit->second;
				} else {
					auto m = std::make_shared<Media>(label);
					medias_[label] = m;
					return m;
				}
			}
		}
		// 2. if p.ssrcs has value, find label from ssrc by using trackid_label_map_
		for (auto kv : p.ssrcs) {
			if (kv.second.track_id.empty()) {
				continue;
			}
			auto lit = trackid_label_map_.find(kv.second.track_id);
			if (lit != trackid_label_map_.end()) {
				auto &label = lit->second;
				auto mit = medias_.find(label);
				if (mit != medias_.end()) {
					return mit->second;
				} else {
					auto m = std::make_shared<Media>(label);
					medias_[label] = m;
					return m;
				}
			}
		}
		// if label is found from rid or ssrc, find media from medias_ by using label
		// otherwise, return empty shared_ptr
		return nullptr;
	}
	int Handler::Produce(const std::string &id, const Parameters &p) {
		auto lit = mid_label_map_.find(p.mid);
		if (lit == mid_label_map_.end()) {
			QRPC_LOGJ(error, {{"ev","failed to find mid label"},{"mid",p.mid}});
			return QRPC_EINVAL;
		}
		auto gid = ProducerFactory::GenerateId(id, lit->second, p.kind);
		for (auto kv : p.ssrcs) {
			ssrc_trackid_map_[kv.first] = kv.second.track_id;
		}
		auto producer = producer_factory_.Create(gid, p);
		// auto &fbb = GetFBB();
		// fbb.Finish(producer->FillBuffer(fbb));
		// puts(Dump<FBS::Producer::DumpResponse>("producer", "FBS.Producer.DumpResponse", fbb).c_str());
		return producer != nullptr ? QRPC_OK : QRPC_EINVAL;
	}
	bool Handler::SetExtensionId(uint8_t id, const std::string &uri) {
		if (uri == "urn:ietf:params:rtp-hdrext:toffset") {
			ext_ids().toffset = id;
		} else if (uri == "http://www.webrtc.org/experiments/rtp-hdrext/abs-send-time") {
			ext_ids().absSendTime = id;
		} else if (uri == "urn:3gpp:video-orientation") {
			ext_ids().videoOrientation = id;
		} else if (uri == "http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01") {
			ext_ids().transportWideCc01 = id;
		} else if (uri == "urn:ietf:params:rtp-hdrext:sdes:mid") {
			ext_ids().mid = id;
		} else if (uri == "urn:ietf:params:rtp-hdrext:sdes:rtp-stream-id") {
			ext_ids().rid = id;
		} else if (uri == "urn:ietf:params:rtp-hdrext:sdes:repaired-rtp-stream-id") {
			ext_ids().rrid = id;
		} else if (uri == "urn:ietf:params:rtp-hdrext:ssrc-audio-level") {
			ext_ids().ssrcAudioLevel = id;
		} else if (uri == "http://tools.ietf.org/html/draft-ietf-avtext-framemarking-07") {
			ext_ids().frameMarking07 = id;
		} else if (uri == "urn:ietf:params:rtp-hdrext:framemarking") {
			ext_ids().frameMarking = id;
		} else if (uri == "http://www.webrtc.org/experiments/rtp-hdrext/abs-capture-time") {
			ext_ids().absCaptureTime = id;
		} else if (
			uri == "http://www.webrtc.org/experiments/rtp-hdrext/playout-delay"
		) {
			// mediasoup ignored but supported (why?)
			return true;
		} else if (
			uri == "http://www.webrtc.org/experiments/rtp-hdrext/video-content-type" ||
			uri == "http://www.webrtc.org/experiments/rtp-hdrext/video-timing" ||
			uri == "http://www.webrtc.org/experiments/rtp-hdrext/color-space" ||
			uri == "https://aomediacodec.github.io/av1-rtp-spec/#dependency-descriptor-rtp-header-extension" ||
			uri == "http://www.webrtc.org/experiments/rtp-hdrext/video-layers-allocation00"
		) {
			// ignored and not supported
			return false;
		} else {
			QRPC_LOGJ(warn, {{"ev","unknown extmap uri"},{"uri", uri}});
			ASSERT(false);
			return false;
		}
		return true;
	}
}
}