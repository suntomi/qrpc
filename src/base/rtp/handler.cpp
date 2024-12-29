#include "base/rtp/handler.h"
#include "base/webrtc/mpatch.h"
#include "base/logger.h"
#include "base/string.h"

#include "base/webrtc/mpatch.h"
#include "base/rtp/parameters.h"

#include <FBS/worker.h>
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
#include "Settings.hpp"
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
		{ FBS::Request::Method::WORKER_UPDATE_SETTINGS, FBS::Request::Body::Worker_UpdateSettingsRequest }
		// more to come if needed
	};

	Handler::MediaStreamConfig::ControlOptions::ControlOptions(const json &j) {
		if (j.is_object()) {
			if (j.contains("pause")) {
				pause = j["pause"].get<bool>();
			}
		}
	}
	bool Handler::MediaStreamConfig::GenerateCN(std::string &cname) const {
		if (mid == RTC::RtpProbationGenerator::GetMidValue()) {
			cname = mid;
		} else {
			auto parsed = str::Split(media_path, "/");
			if (parsed.size() < 3) {
				cname = str::Format("invalid media_path: %s", media_path.c_str());
				ASSERT(false);
				return false;
			}
			cname = parsed[0];
			if (cname.empty()) {
				cname = str::Format("invalid media_path: %s", media_path.c_str());
				ASSERT(false);
				return false;
			}
		}
		return true;
	}

	void Handler::ConfigureLogging(const std::string &log_level, const std::vector<std::string> &log_tags) {
		auto &fbb = GetFBB();
		std::vector<::flatbuffers::Offset<::flatbuffers::String>> log_tags_packed;
		for (const auto &tag : log_tags) {
			log_tags_packed.push_back(fbb.CreateString(tag));
		}
		auto req = Handler::CreateRequest(fbb, FBS::Request::Method::WORKER_UPDATE_SETTINGS,
			FBS::Worker::CreateUpdateSettingsRequestDirect(fbb, log_level.c_str(), &log_tags_packed)
		);
		Settings::HandleRequest(&req);
	}
	
	const FBS::Transport::Options* Handler::TransportOptions(const Config &c) {
		auto &fbb = GetFBB();
		fbb.Finish(FBS::Transport::CreateOptions(
			fbb, false, std::nullopt, c.initial_outgoing_bitrate, false)
		);
		return ::flatbuffers::GetRoot<FBS::Transport::Options>(fbb.GetBufferPointer());
	}
	const std::vector<Parameters::MediaKind> &Handler::SupportedMediaKind() {
		static const std::vector<Parameters::MediaKind> kinds = {
			Parameters::MediaKind::AUDIO, Parameters::MediaKind::VIDEO
		};
		return kinds;
	}
	bool Handler::PrepareConsume(
      Handler &peer, const std::vector<std::string> &parsed_media_path,
			const std::map<rtp::Parameters::MediaKind, MediaStreamConfig::ControlOptions> options_map,
			MediaStreamConfigs &media_stream_configs, std::vector<uint32_t> &generated_ssrcs
	) {
		const auto &label = parsed_media_path[1];
		for (const auto k : SupportedMediaKind()) {
			auto media_path = parsed_media_path[0] + "/" + label + "/" + Parameters::FromMediaKind(k);
			auto ccit = std::find_if(media_stream_configs.begin(), media_stream_configs.end(), [&media_path](const auto &c) {
				return c.media_path == media_path && c.sender(); // if sender of same media-path already exists, skip.
			});
			QRPC_LOGJ(info, {{"ev","check consume config"},{"path",media_path},{"exists",ccit != media_stream_configs.end()}});
			if (ccit != media_stream_configs.end()) {
				QRPC_LOGJ(info, {
					{"ev","ignore media because already prepared"},
					{"label",label},{"kind",Parameters::FromMediaKind(k)}
				});
				continue;
			}
			auto capit = listener_.capabilities().find(k);
			if (capit == listener_.capabilities().end()) {
				QRPC_LOGJ(info, {
					{"ev","ignore media type which is not supported"},
					{"label",label},{"kind",Parameters::FromMediaKind(k)}
				});
				ASSERT(false);
				continue;
			}
			auto consumed_producer = peer.FindProducer(label, k);
			if (consumed_producer == nullptr) {
				QRPC_LOGJ(info, {
					{"ev","ignore media because corresponding producer of peer not found"},
					{"label",label},{"kind",Parameters::FromMediaKind(k)},{"peer",peer.rtp_id()}
				});
				continue;
			}
			auto &config = media_stream_configs.emplace_back();
			config.direction = MediaStreamConfig::Direction::SEND;
			config.media_path = media_path;
			config.options = options_map.find(k) == options_map.end() ?
				MediaStreamConfig::ControlOptions() : options_map.find(k)->second;
			// copy additional parameter from producer that affects sdp generation
			config.kind = k;
			config.network = consumed_producer->params().network;
			config.rtp_proto = consumed_producer->params().rtp_proto;
			config.ssrc_seed = consumed_producer->params().ssrc_seed;
			// generate rtp parameter from this handler_'s capabality (of corresponding producer) and consumed_producer's encodings
			if (!Producer::consumer_params(consumed_producer->params(), capit->second, config)) {
				QRPC_LOGJ(error, {{"ev","fail to generate cosuming params"}});
				ASSERT(false);
				continue;
			}
			// generate unique mid from own consumer factory
			auto mid = GenerateMid();
			config.mid = mid;
			config.rtcp.cname = peer.cname();
			config.GetGeneratedSsrc(generated_ssrcs);
			// if video consumer and there is no probator mid, generate probator mid => param pair
			ccit = std::find_if(media_stream_configs.begin(), media_stream_configs.end(), [](const auto &c) {
				return c.mid == RTC::RtpProbationGenerator::GetMidValue() && c.sender();
			});
			if (k == Parameters::MediaKind::VIDEO && ccit == media_stream_configs.end()) {
				media_stream_configs.emplace_back(MediaStreamConfig(config.ToProbator(), MediaStreamConfig::Direction::SEND));
			}
			// update mid label map
			UpdateMidMediaPathMap(config.mid, media_path);
		}
		QRPC_LOGJ(info, {{"ev","set consume configs"},{"configs",media_stream_configs.size()}});
		return true;
	}

	bool Handler::Consume(
		Handler &peer, const std::string &label, const MediaStreamConfig &config
	) {
		auto cid = ConsumerFactory::GenerateId(rtp_id(), peer.rtp_id(), label, config.kind);
		auto cit = GetConsumers().find(cid);
		if (cit != GetConsumers().end()) {
			QRPC_LOGJ(info, {{"ev","consumer already created"},{"cid",cid},{"mid",cit->second->GetRtpParameters().mid}});
			return true;
		}
		QRPC_LOGJ(info, {{"ev","create consumer"},{"cid",cid},{"mid",config.mid}});
		const auto &options = config.options;
		const auto &kind = config.kind;
		// false for creating pipe consumer: TODO: support pipe consumer
		auto consumer = consumer_factory_.Create(peer, label, kind, config, options.pause, false);
		if (consumer == nullptr) {
				QRPC_LOGJ(error, {{"ev","fail to create consumer"}});
				ASSERT(false);
				return false;
		}
		for (auto &kv : GetConsumers()) {
			QRPC_LOGJ(info, {{"ev","consumer list"},{"cid",kv.first},{"mid",kv.second->GetRtpParameters().mid}});			
		}
		// auto &fbb = GetFBB();
		// fbb.Finish(consumer_factory_.FillBuffer(consumer, fbb));
		// puts(Dump<FBS::Consumer::DumpResponse>("consumer", "FBS.Consumer.DumpResponse", fbb).c_str());
		return true;
	}
	struct ControlSet {
		static const FBS::Request::Method DOES_NOT_SUPPORT = FBS::Request::Method::WORKER_CLOSE;
		FBS::Request::Method producer_method, consumer_method;
	};
	static std::map<std::string, ControlSet> g_controls = {
		{"pause", {.producer_method = FBS::Request::Method::PRODUCER_PAUSE, .consumer_method = FBS::Request::Method::CONSUMER_PAUSE}},
		{"resume", {.producer_method = FBS::Request::Method::PRODUCER_RESUME, .consumer_method = FBS::Request::Method::CONSUMER_RESUME}}
	};
	bool Handler::ControlStream(const std::string &label, const std::string &control) {
		auto cit = g_controls.find(control);
		if (cit == g_controls.end()) {
			QRPC_LOGJ(error, {{"ev","undefined control"},{"control",control}});
			ASSERT(false);
			return false;
		}
		auto parsed = str::Split(label, "/");
		if (parsed.size() == 2) { // $label/(audio|video)
			if (cit->second.producer_method == ControlSet::DOES_NOT_SUPPORT) {
				QRPC_LOGJ(info, {{"ev","control for producer does not supported"},{"control",control},{"id",id}});
				return false;
			}
			auto id = ProducerFactory::GenerateId(rtp_id(), parsed[0], Parameters::ToMediaKind(parsed[1]));
			auto producer = FindProducer(id);
			if (producer == nullptr) {
				QRPC_LOGJ(info, {{"ev","no producer found for pause"},{"id",id}});
				return false;
			}
			try {
				auto req = CreateRequest<void>(GetFBB(), cit->second.producer_method);
				producer->HandleRequest(&req);
			} catch (std::exception &e) {
				QRPC_LOGJ(error, {{"ev","fail to control stream"},{"control",control},{"id",id},{"error",e.what()}});
				return false;
			}
			return true;
		} else if (parsed.size() == 3) {
			if (cit->second.consumer_method == ControlSet::DOES_NOT_SUPPORT) {
				QRPC_LOGJ(info, {{"ev","control for consumer does not supported"},{"control",control},{"id",id}});
				return false;
			}
			const auto &peer_rtp_id = listener_.FindRtpIdFrom(parsed[0]);
			if (peer_rtp_id.empty()) {
				QRPC_LOGJ(error, {{"ev","no connection with the cname found"},{"cname",parsed[0]}});
				return false;
			}
			auto id = ConsumerFactory::GenerateId(rtp_id(), peer_rtp_id, parsed[1], Parameters::ToMediaKind(parsed[2]));
			auto consumer = FindConsumer(id);
			if (consumer == nullptr) {
				QRPC_LOGJ(info, {{"ev","no producer found for pause"},{"id",id}});
				return false;
			}
			try {
				auto req = CreateRequest<void>(GetFBB(), cit->second.consumer_method);
				consumer->HandleRequest(&req);
			} catch (std::exception &e) {
				QRPC_LOGJ(error, {{"ev","fail to control stream"},{"control",control},{"id",id},{"error",e.what()}});
				return false;
			}
			return true;
		}
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
	void Handler::Close() {
		// close all producer (and consumer) to notice consumers that streams are closed
		// then consumers try to re-subscribe same produced stream and connect to new connection
		RTC::Transport::CloseProducersAndConsumers();
	}
	int Handler::Produce(const std::string &id, const Parameters &p) {
		auto l = FindLabelByMid(p.mid);
		if (l.empty()) {
			QRPC_LOGJ(error, {{"ev","failed to find mid label"},{"mid",p.mid}});
			return QRPC_EINVAL;
		}
		auto gid = ProducerFactory::GenerateId(id, l, p.kind);
		for (auto kv : p.ssrcs) {
			ssrc_trackid_map_[kv.first] = kv.second.track_id;
		}
		auto producer = producer_factory_.Create(gid, p);
		// auto &fbb = GetFBB();
		// fbb.Finish(producer->FillBuffer(fbb));
		// puts(Dump<FBS::Producer::DumpResponse>("producer", "FBS.Producer.DumpResponse", fbb).c_str());
		return producer != nullptr ? QRPC_OK : QRPC_EINVAL;
	}
	void Handler::UpdateByCapability(const Capability &cap) {
		for (const auto &he : cap.headerExtensions) {
			SetExtensionId(he.id, he.type);
		}
	}
	bool Handler::SetExtensionId(uint8_t id, RTC::RtpHeaderExtensionUri::Type uri) {
		if (uri == RTC::RtpHeaderExtensionUri::Type::TOFFSET) {
			ext_ids().toffset = id;
		} else if (uri == RTC::RtpHeaderExtensionUri::Type::ABS_SEND_TIME) {
			ext_ids().absSendTime = id;
		} else if (uri == RTC::RtpHeaderExtensionUri::Type::VIDEO_ORIENTATION) {
			ext_ids().videoOrientation = id;
		} else if (uri == RTC::RtpHeaderExtensionUri::Type::TRANSPORT_WIDE_CC_01) {
			ext_ids().transportWideCc01 = id;
		} else if (uri == RTC::RtpHeaderExtensionUri::Type::MID) {
			ext_ids().mid = id;
		} else if (uri == RTC::RtpHeaderExtensionUri::Type::RTP_STREAM_ID) {
			ext_ids().rid = id;
		} else if (uri == RTC::RtpHeaderExtensionUri::Type::REPAIRED_RTP_STREAM_ID) {
			ext_ids().rrid = id;
		} else if (uri == RTC::RtpHeaderExtensionUri::Type::SSRC_AUDIO_LEVEL) {
			ext_ids().ssrcAudioLevel = id;
		} else if (uri == RTC::RtpHeaderExtensionUri::Type::FRAME_MARKING_07) {
			ext_ids().frameMarking07 = id;
		} else if (uri == RTC::RtpHeaderExtensionUri::Type::FRAME_MARKING) {
			ext_ids().frameMarking = id;
		} else if (uri == RTC::RtpHeaderExtensionUri::Type::ABS_CAPTURE_TIME) {
			ext_ids().absCaptureTime = id;
		} else if (
			uri == RTC::RtpHeaderExtensionUri::Type::PLAYOUT_DELAY
		) {
			// mediasoup ignored but supported (why?)
			return true;
		} else {
			// "http://www.webrtc.org/experiments/rtp-hdrext/video-content-type"
			// "http://www.webrtc.org/experiments/rtp-hdrext/video-timing"
			// "http://www.webrtc.org/experiments/rtp-hdrext/color-space"
			// "https://aomediacodec.github.io/av1-rtp-spec/#dependency-descriptor-rtp-header-extension"
			// "http://www.webrtc.org/experiments/rtp-hdrext/video-layers-allocation00"
			QRPC_LOGJ(warn, {{"ev","unknown extmap uri"},{"uri", uri}});
			ASSERT(false);
			return false;
		}
	}
}
}