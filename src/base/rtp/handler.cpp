#include "base/rtp/handler.h"
#include "base/webrtc/mpatch.h"
#include "base/logger.h"
#include "base/stream.h"
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
		{ FBS::Request::Method::WORKER_UPDATE_SETTINGS, FBS::Request::Body::Worker_UpdateSettingsRequest },
		{ FBS::Request::Method::TRANSPORT_CLOSE_CONSUMER, FBS::Request::Body::Transport_CloseConsumerRequest },
		// more to come if needed
	};

	MediaStreamConfig::ControlOptions::ControlOptions(const json &j) {
		if (j.is_object()) {
			if (j.contains("pause")) {
				pause = j["pause"].get<bool>();
			}
		}
	}
	bool MediaStreamConfig::GenerateCN(std::string &cname) const {
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
      Handler &peer, const std::string &local_path, const std::optional<rtp::Parameters::MediaKind> &media_kind,
			const std::map<rtp::Parameters::MediaKind, MediaStreamConfig::ControlOptions> options_map, bool sync,
			MediaStreamConfigs &media_stream_configs, std::vector<uint32_t> &generated_ssrcs
	) {
		auto path = peer.cname() + "/" + local_path;
		const auto &kinds = media_kind.has_value() ? std::vector<Parameters::MediaKind>{media_kind.value()} : SupportedMediaKind();
		for (const auto k : kinds) {
			auto kind = Parameters::FromMediaKind(k);
			auto media_path = path + kind;
			auto capit = listener_.capabilities().find(k);
			if (capit == listener_.capabilities().end()) {
				QRPC_LOGJ(info, {{"ev","ignore media type which is not supported"},{"path",path},{"kind",kind}});
				ASSERT(false);
				continue;
			}
			auto ccit = std::find_if(media_stream_configs.begin(), media_stream_configs.end(), [&media_path](const auto &c) {
				return !c.deleted && c.media_path == media_path && c.sender(); // if sender of same media-path already exists, skip.
			});
			QRPC_LOGJ(info, {{"ev","check consume config"},{"media_path",media_path},{"exists",ccit != media_stream_configs.end()}});
			if (ccit != media_stream_configs.end()) {
				if (sync) {
					ccit->Reset();
					// to find producer from peer, need to use local_path (path without cname and media kind)
					auto consumed_producer = peer.FindProducerByPath(local_path + kind);
					if (consumed_producer == nullptr) {
						QRPC_LOGJ(info, {
							{"ev","ignore media because corresponding producer of peer not found"},
							{"path",path + kind},{"peer",peer.rtp_id()}
						});
						ASSERT(false);
						continue;
					}
					auto &config = *ccit;
					config.encodings.clear();
					config.codecs.clear();
					config.headerExtensions.clear();
					// generate rtp parameter from this handler_'s capabality (of corresponding producer) and consumed_producer's encodings
					if (!Producer::consumer_params(consumed_producer->params(), capit->second, config)) {
						QRPC_LOGJ(error, {{"ev","fail to generate cosuming params"}});
						ASSERT(false);
						continue;
					}
				} else {
					QRPC_LOGJ(info, {{"ev","ignore media because already prepare"},{"media_path",media_path},{"sync",sync}});
				}
				continue;
			}
			// to find producer from peer, need to use local_path (path without cname and media kind)
			auto consumed_producer = peer.FindProducerByPath(local_path + kind);
			if (consumed_producer == nullptr) {
				QRPC_LOGJ(info, {
					{"ev","ignore media because corresponding producer of peer not found"},
					{"path",path + kind},{"peer",peer.rtp_id()}
				});
				ASSERT(false);
				continue;
			}
			auto &config = media_stream_configs.emplace_back();
			config.direction = MediaStreamConfig::Direction::SEND;
			config.media_path = path + kind;
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
			UpdateMidMediaPathMap(config);
		}
		QRPC_LOGJ(info, {{"ev","set consume configs"},{"configs",media_stream_configs.size()}});
		return true;
	}

	bool Handler::Consume(Handler &peer, const MediaStreamConfig &config, std::string &error) {
		if (config.deleted) {
			QRPC_LOGJ(info, {{"ev","ignore deleted config"},{"mid",config.mid},{"path",config.media_path}});
			return true;
		}
		const auto &path = config.media_path;
		auto cid = ConsumerFactory::GenerateId(rtp_id(), path);
		auto cit = consumers().find(cid);
		if (cit != consumers().end()) {
			auto parsed = str::Split(cit->second->producerId, "/");
			if (parsed.size() < 4) {
				error = "invalid producer id:" + cit->second->producerId;
				QRPC_LOGJ(error, {{"ev","invalid producer id"},{"id",cit->second->producerId}});
				ASSERT(false);
				return false;
			} else if (parsed[2] != peer.rtp_id()) {
				QRPC_LOGJ(info, {{"ev","target producer replaced"},{"old",parsed[2]},{"new",peer.rtp_id()}});
				CloseConsumer(cit->second);
			} else {
				QRPC_LOGJ(info, {{"ev","consumer already created"},{"cid",cid},{"mid",cit->second->GetRtpParameters().mid},{"old",parsed[2]},{"new",peer.rtp_id()}});
				return true;
			}
		}
		QRPC_LOGJ(info, {{"ev","create consumer"},{"cid",cid},{"mid",config.mid},{"target_rtp_id",peer.rtp_id()}});
		// false for creating pipe consumer: TODO: support pipe consumer when its required
		auto consumer = consumer_factory_.Create(peer, config, false);
		if (consumer == nullptr) {
				QRPC_LOGJ(error, {{"ev","fail to create consumer"}});
				peer.DumpChildren();
				return false;
		}
		// for (auto &kv : consumers()) {
		// 	QRPC_LOGJ(info, {{"ev","consumer list"},{"cid",kv.first},{"mid",kv.second->GetRtpParameters().mid}});			
		// }
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
		{"resume", {.producer_method = FBS::Request::Method::PRODUCER_RESUME, .consumer_method = FBS::Request::Method::CONSUMER_RESUME}},
		{"request_key_frame",{.producer_method = ControlSet::DOES_NOT_SUPPORT, .consumer_method = FBS::Request::Method::CONSUMER_REQUEST_KEY_FRAME}}
	};
	bool Handler::ControlStream(const std::string &path, const std::string &control, bool &is_producer, std::string &error) {
		auto cit = g_controls.find(control);
		if (cit == g_controls.end()) {
			error = "undefined control:" + control;
			QRPC_LOGJ(error, {{"ev","undefined control"},{"control",control}});
			ASSERT(false);
			return false;
		}
		auto producer = FindProducerByPath(path);
		if (producer != nullptr) {
			is_producer = true;
			if (cit->second.producer_method == ControlSet::DOES_NOT_SUPPORT) {
				error = "producer " + path + " does not support the control:" + control;
				QRPC_LOGJ(info, {{"ev","control for consumer does not supported"},{"control",control},{"cname",cname()},{"path",path}});
				return false;
			}
			try {
				auto req = CreateRequest<void>(GetFBB(), cit->second.producer_method);
				producer->HandleRequest(&req);
			} catch (std::exception &e) {
				error = "request failure:" + std::string(e.what());
				QRPC_LOGJ(error, {{"ev","fail to control stream"},{"control",control},{"id",id},{"error",e.what()}});
				return false;
			}
		} else {
			if (cit->second.consumer_method == ControlSet::DOES_NOT_SUPPORT) {
				error = "consumer " + path + " does not support the control:" + control;
				QRPC_LOGJ(info, {{"ev","control for consumer does not supported"},{"control",control},{"cname",cname()},{"path",path}});
				return false;
			}
			auto consumer = FindConsumerByPath(path);
			if (consumer == nullptr) {
				error = "no consumer found:" + path;
				QRPC_LOGJ(info, {{"ev","no producer found for pause"},{"path",path}});
				return false;
			}
			is_producer = false;
			if (cit->second.consumer_method == ControlSet::DOES_NOT_SUPPORT) {
				error = "consumer " + path + " does not support the control:" + control;
				QRPC_LOGJ(info, {{"ev","control for consumer does not supported"},{"control",control},{"cname",cname()},{"path",path}});
				return false;
			}
			try {
				auto req = CreateRequest<void>(GetFBB(), cit->second.consumer_method);
				consumer->HandleRequest(&req);
			} catch (std::exception &e) {
				error = "request failure:" + std::string(e.what());
				QRPC_LOGJ(error, {{"ev","fail to control stream"},{"control",control},{"cname",cname()},{"error",e.what()}});
				return false;
			}
		}
		return true;
	}
	bool Handler::Sync(const std::string &path, std::string &error) {
		auto *c = FindConsumerByPath(path);
		if (c == nullptr) {
			error = "consumer to sync not found";
			QRPC_LOGJ(error, {{"ev",error},{"path",path}});
			ASSERT(false);
			return false;
		}
		c->ProducerPaused();
		c->ProducerResumed();
		return true;
	}
	bool Handler::Ping(std::string &error) {
		for (const auto &kv : producers()) {
      auto it = mid_media_path_map_.find(kv.second->GetRtpParameters().mid);
			if (it == mid_media_path_map_.end()) {
				QRPC_LOGJ(info, {{"ev","no path data"},{"mid",kv.second->GetRtpParameters().mid}});
				ASSERT(false);
				continue;
			}
			auto pl = json({{"fn","ping"},{"args",{{"path",cname() + "/" + it->second}}}}).dump();
			SendToConsumersOf(kv.second, Stream::SYSCALL_NAME, pl.c_str(), pl.size());
		}
		return true;
	}
	bool Handler::Pause(const std::string &path, std::string &error) {
		bool is_producer = false;
		if (ControlStream(path, "pause", is_producer, error)) {
			if (is_producer) {
				auto pl = json({{"args",{{"path",cname() + "/" + path}}},{"fn","remote_pause"}}).dump();
				SendToConsumersOf(path, Stream::SYSCALL_NAME, pl.c_str(), pl.size());
			}
			return true;
		}
		return false;
	}
	bool Handler::Resume(const std::string &path, std::string &error) {
		bool is_producer = false;
		if (ControlStream(path, "resume", is_producer, error)) {
			if (is_producer) {
				auto pl = json({{"args",{{"path",cname() + "/" + path}}},{"fn","remote_resume"}}).dump();
				SendToConsumersOf(path, Stream::SYSCALL_NAME, pl.c_str(), pl.size());
			}
			return true;
		}
		return false;
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
	std::shared_ptr<Media> Handler::FindFrom(const std::string &path, bool consumer) {
		auto parsed = str::Split(path, "/");
		if (consumer) {
			if (parsed.size() < 3) {
				QRPC_LOGJ(error, {{"ev","invalid consumer path"},{"path",path}});
				ASSERT(false);
				return nullptr;
			}
		} else if (parsed.size() < 2) {
			QRPC_LOGJ(error, {{"ev","invalid producer path"},{"path",path}});
			ASSERT(false);
			return nullptr;
		}
		// remove last element (audio/video)
		parsed.erase(parsed.end() - 1);
		// if consumer, remove first element (cname)
		if (consumer) {
			parsed.erase(parsed.begin());
		}
		auto lpath = str::Join(parsed, "/");
		auto lit = medias_.find(lpath);
		if (lit == medias_.end()) {
			auto m = std::make_shared<Media>(lpath);
			medias_[lpath] = m;
			return m;
		}
		return lit->second;
	}
	std::shared_ptr<Media> Handler::FindFrom(const Parameters &p, bool consumer) {
		auto pit = mid_media_path_map_.find(p.mid);
		if (pit == mid_media_path_map_.end()) {
			QRPC_LOGJ(error, {{"ev","failed to find path from mid"},{"mid",p.mid}});
			return nullptr;
		}
		return FindFrom(pit->second, consumer);
	}
	void Handler::Close() {
		// close all producer (and consumer) to notice consumers that streams are closed
		// then consumers try to re-subscribe same produced stream and connect to new connection
		RTC::Transport::CloseProducersAndConsumers();
	}
	void Handler::SendToConsumersOf(
		const RTC::Producer *p, const std::string &stream_label, const char *data, size_t len
	) {
		for (auto *c : router_.GetConsumersOf(p)) {
			auto &h = ConsumerFactory::HandlerFrom(c);
			h.SendToStream(stream_label, data, len);
		}
	}
	void Handler::CloseConsumer(Consumer *c) {
		auto &fbb = GetFBB();
		HandleRequest(fbb, FBS::Request::Method::TRANSPORT_CLOSE_CONSUMER, 
			FBS::Transport::CreateCloseConsumerRequestDirect(fbb, c->id.c_str()));
	}
	void Handler::DumpChildren() {
#if !defined(NDEBUG)
		puts(str::Format("================ dump children of %s ================", rtp_id().c_str()).c_str());
		for (auto &kv : consumers()) {
			auto &fbb = GetFBB();
			fbb.Finish(consumer_factory_.FillBuffer(kv.second, fbb));
			puts(Dump<FBS::Consumer::DumpResponse>("consumer", "FBS.Consumer.DumpResponse", fbb).c_str());
		}
		for (auto &kv : producers()) {
			auto &fbb = GetFBB();
			fbb.Finish(kv.second->FillBuffer(fbb));
			puts(Dump<FBS::Producer::DumpResponse>("producer", "FBS.Producer.DumpResponse", fbb).c_str());
		}
		puts("================================");
#endif
	}
	int Handler::Produce(const MediaStreamConfig &p) {
		for (auto kv : p.ssrcs) {
			ssrc_trackid_map_[kv.first] = kv.second.track_id;
		}
		auto producer = producer_factory_.Create(p);
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
		} else if (uri == RTC::RtpHeaderExtensionUri::Type::PLAYOUT_DELAY) {
			// mediasoup ignored but supported (why?)
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
		return true;
	}
}
}