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
  thread_local Shared Handler::shared_;
	thread_local Handler::RouterListener router_listener_;
	thread_local Router Handler::router_(&shared_.get(), &router_listener_);
	const std::map<FBS::Request::Method, FBS::Request::Body> Handler::payload_map_ = {
		{ FBS::Request::Method::TRANSPORT_CONSUME, FBS::Request::Body::Transport_ConsumeRequest },
		{ FBS::Request::Method::TRANSPORT_PRODUCE, FBS::Request::Body::Transport_ProduceRequest },
		{ FBS::Request::Method::WORKER_UPDATE_SETTINGS, FBS::Request::Body::Worker_UpdateSettingsRequest },
		{ FBS::Request::Method::TRANSPORT_CLOSE_CONSUMER, FBS::Request::Body::Transport_CloseConsumerRequest },
		{ FBS::Request::Method::TRANSPORT_CLOSE_PRODUCER, FBS::Request::Body::Transport_CloseProducerRequest },
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
	bool Handler::CloseMedia(
		const std::string &path, const std::optional<Parameters::MediaKind> &media_kind,
		MediaStreamConfigs &media_stream_configs, std::vector<std::string> &closed_paths, std::string &error
	) {
		const auto &kinds = media_kind.has_value() ? std::vector<Parameters::MediaKind>{media_kind.value()} : SupportedMediaKind();
		for (const auto k : kinds) {
			auto kind = Parameters::FromMediaKind(k);
			auto media_path = path + kind;
			auto ccit = std::find_if(media_stream_configs.begin(), media_stream_configs.end(), [&media_path](const auto &c) {
				return c.media_path == media_path;
			});
			if (ccit == media_stream_configs.end()) {
				error = "media not found";
				QRPC_LOGJ(error, {{"ev","media not found"},{"path",media_path}});
				ASSERT(false);
				return false;
			}
			if (ccit->closed()) {
				QRPC_LOGJ(info, {{"ev","media already closed"},{"path",media_path}});
				return true;
			}
			if (!CloseStream(*ccit, error)) {
				QRPC_LOGJ(error, {{"ev","fail to close media"},{"path",media_path},{"error",error}});
				return false;
			}
			QRPC_LOGJ(info, {{"ev","media closed"},{"path",media_path}});
			closed_paths.push_back(media_path);
			ccit->Close();
		}
		return true;
	}
	bool Handler::PrepareConsume(
      Handler &peer, const std::string &local_path, const std::optional<Parameters::MediaKind> &media_kind,
			const std::map<Parameters::MediaKind, MediaStreamConfig::ControlOptions> options_map, bool sync,
			MediaStreamConfigs &media_stream_configs, std::map<std::string,Consumer*> &created_consumers
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
			// to find producer from peer, need to use local_path (path without cname and media kind)
			const auto *consumed_producer = peer.FindProducerByPath(local_path + kind);
			if (consumed_producer == nullptr) {
				QRPC_LOGJ(info, {
					{"ev","ignore media because corresponding producer of peer not found"},
					{"path",local_path + kind},{"peer",peer.rtp_id()}
				});
				continue;
			}
			auto params = consumed_producer->params();
			if (params == nullptr) {
				QRPC_LOGJ(error, {
					{"ev","fail to get producer rtp params"},
					{"path",local_path + kind},{"mid",consumed_producer->GetRtpParameters().mid}
				});
				ASSERT(false);
				continue;
			}
			// set place holder for consumer that may be created
			created_consumers[media_path] = nullptr;
			// check if consumer of same media-path already exists
			auto ccit = std::find_if(media_stream_configs.begin(), media_stream_configs.end(), [&media_path](const auto &c) {
				return !c.closed() && c.sender() && c.media_path == media_path; // if active sender of same media-path already exists, skip.
			});
			QRPC_LOGJ(info, {{"ev","check consume config"},{"media_path",media_path},{"exists",ccit != media_stream_configs.end()}});
			if (ccit != media_stream_configs.end()) {
				if (!sync) {
					QRPC_LOGJ(info, {{"ev","ignore media because already prepare"},{"media_path",media_path},{"sync",sync}});
					continue;
				} else {
					auto cid = ConsumerFactory::GenerateId(rtp_id(), media_path);
					auto cit = consumers().find(cid);
					if (cit != consumers().end()) {
						auto parsed = str::Split(cit->second->producerId, "/");
						if (parsed.size() < 4) {
							QRPC_LOGJ(error, {{"ev","invalid producer id"},{"id",cit->second->producerId}});
							ASSERT(false);
							continue;
						} else if (parsed[2] == peer.rtp_id()) {
							QRPC_LOGJ(info, {{"ev","ignore sync media because already active"},{"media_path",media_path}});
							continue;
						}
					}
				}
				ccit->Reconnect();
			} else {
				// find empty place holder for consumer
				ccit = std::find_if(media_stream_configs.begin(), media_stream_configs.end(), [k](const auto &c) {
					return c.closed() && c.sender() && c.kind == k; // if sender of same media-path already exists, skip.
				});
				if (ccit != media_stream_configs.end()) {
					QRPC_LOGJ(info, {{"ev","reuse closed media config"},{"old_media_path",ccit->media_path},{"new_media_path",media_path}});
					ccit->Reuse();
				}
			}
			auto &config = ccit != media_stream_configs.end() ? *ccit : media_stream_configs.emplace_back();
			// initialization depends on the slot is from reuse/reconnect or not
			if (config.mid.empty()) { // new slot
				// generate unique mid from own consumer factory
				auto mid = GenerateMid();
				config.mid = mid;
			} else { // reuse/reconnect slot
				config.Reset();
			}
			config.direction = MediaStreamConfig::Direction::SEND;
			config.media_path = path + kind;
			config.options = options_map.find(k) == options_map.end() ?
				MediaStreamConfig::ControlOptions() : options_map.find(k)->second;
			// copy additional parameter from producer that affects sdp generation
			config.kind = k;
			config.network = params->network;
			config.rtp_proto = params->rtp_proto;
			// generate rtp parameter from this handler_'s capabality (of corresponding producer) and consumed_producer's encodings
			if (!Producer::consumer_params(*params, capit->second, config)) {
				QRPC_LOGJ(error, {{"ev","fail to generate cosuming params"}});
				ASSERT(false);
				continue;
			}
			config.rtcp.cname = peer.cname();
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
	void Handler::TryRidComplement(uint32_t ssrc) {
		auto it = ssrc_stream_recovery_map_.find(ssrc);
		if (it != ssrc_stream_recovery_map_.end()) {
			it->second.try_rid_complement = true;
			auto rid = it->second.rid;
			auto roc = it->second.rtp_roc;
			QRPC_LOGJ(info, {{"ev","try complement rid"},{"ssrc",ssrc},{"rid",rid},{"roc",roc}});
		}
	}
	void Handler::FixListnerSsrcMap(Producer *p, uint32_t old_ssrc, uint32_t new_ssrc) {
		auto it = rtpListener.ssrcTable.find(old_ssrc);
		if (it != rtpListener.ssrcTable.end()) {
			rtpListener.ssrcTable.erase(it);
		}
		rtpListener.ssrcTable[new_ssrc] = p;
	}
	bool Handler::CloseStream(MediaStreamConfig &config, std::string &error) {
		if (config.sender()) {
			auto *c = FindConsumerByPath(config.media_path);
			if (c != nullptr) {
				CloseConsumer(c);
			} else {
				error = "no consumer found for path:" + config.media_path;
				QRPC_LOGJ(error, {{"ev","no consumer found for path"},{"path",config.media_path}});
				return false;
			}
		} else if (config.receiver()) {
			auto *p = FindProducerByPath(config.media_path);
			if (p != nullptr) {
				// preserve ssrcs related with the producer. because if chrome has 2 tabs which connects to qrpc server, 
				// later one does not send rid/mid to the server. so remember ssrc => rid mapping of previous setting and 
				auto &map_ref = ssrc_stream_recovery_map_;
				for (const auto &kv : p->GetRtpStreams()) {
					const auto &rid = kv.first->GetRid();
					const auto ssrc = kv.first->GetSsrc();
					if (!rid.empty()) {
						// also records rollover counter because chrome keep rollover counter even if media section is reused by another track.
						uint32_t roc;
						if (!listener().GetRtpRoc(ssrc, roc, config.direction)) {
							QRPC_LOGJ(info, {{"ev", "fail to get roc"},{"ssrc", ssrc},{"rid", rid}});
							ASSERT(false);
							continue;
						}
						auto it = map_ref.find(ssrc);
						if (it == map_ref.end()) {
							QRPC_LOGJ(info, {{"ev","new complement rid mapping"},{"ssrc",ssrc},{"rid",rid},{"media_path",config.media_path},{"roc",roc}});
							map_ref[ssrc] = { .rid = rid, .rtp_roc = roc, .try_rid_complement = true, };
						} else {
							QRPC_LOGJ(info, {{"ev","update complement rid mapping"},{"ssrc",ssrc},{"rid",rid},{"media_path",config.media_path},{"roc",roc}});
							it->second.try_rid_complement = true;
							it->second.rtp_roc = roc;
						}
					}
				}
				CloseProducer(p);
			} else {
				error = "no producer found for path:" + config.media_path;
				QRPC_LOGJ(error, {{"ev","no producer found for path"},{"path",config.media_path}});
				return false;
			}
		}
		return true;
	}
	void Handler::CloseConsumer(Consumer *c) {
		auto &fbb = GetFBB();
		HandleRequest(fbb, FBS::Request::Method::TRANSPORT_CLOSE_CONSUMER, 
			FBS::Transport::CreateCloseConsumerRequestDirect(fbb, c->id.c_str()));
	}
	void Handler::CloseProducer(Producer *p) {
		auto pl = json({{"args",{{"path",cname() + "/" + p->media_path()}}},{"fn","close_track"}}).dump();
		auto data = pl.c_str();
		auto len = pl.size();
		for (auto *c : router_.GetConsumersOf(p)) {
			auto &h = ConsumerFactory::HandlerFrom(c);
			h.SendToStream(Stream::SYSCALL_NAME, data, len);
			ConsumerFactory::OnProducerManuallyClosed(c);
		}
		auto &fbb = GetFBB();
		HandleRequest(fbb, FBS::Request::Method::TRANSPORT_CLOSE_PRODUCER, 
			FBS::Transport::CreateCloseProducerRequestDirect(fbb, p->id.c_str()));
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
	void Handler::ReceiveRtpPacket(RTC::RtpPacket* packet) {
		auto ssrc = packet->GetSsrc();
		auto it = ssrc_stream_recovery_map_.find(ssrc);
		if (it != ssrc_stream_recovery_map_.end() && it->second.try_rid_complement) {
			packet->SetRidExtensionId(this->recvRtpHeaderExtensionIds.rid);
			std::string rid;
			if (!packet->ReadRid(rid) || rid.empty()) {
				QRPC_LOGJ(info, {{"ev","complement rid"},{"ssrc",ssrc},{"rid",rid},{"recover_rid",it->second.rid},{"rid_ext_id", ext_ids().rid}});
				packet->UpdateExtensions({
					RTC::RtpPacket::GenericExtension(
						ext_ids().rid, it->second.rid.length(), 
						const_cast<uint8_t *>(reinterpret_cast<const uint8_t *>(it->second.rid.c_str()))
					)
				});
#if !defined(NDEBUG)
				// check rid set correctly
				packet->SetRidExtensionId(this->recvRtpHeaderExtensionIds.rid);
				ASSERT(packet->ReadRid(rid) && rid == it->second.rid);
#endif
			}
			it->second.try_rid_complement = false;
		}
		RTC::Transport::ReceiveRtpPacket(packet);
	}
	Producer *Handler::Produce(const MediaStreamConfig &p) {
		auto producer = producer_factory_.Create(p);
		// auto &fbb = GetFBB();
		// fbb.Finish(producer->FillBuffer(fbb));
		// puts(Dump<FBS::Producer::DumpResponse>("producer", "FBS.Producer.DumpResponse", fbb).c_str());
		return producer;
	}
	bool Handler::ApplyAnswer(const std::string &mid, const RemoteAnswer &answer, std::string &error) {
		auto found = false;
		for (const auto &kv : this->mapProducers) {
			auto p = dynamic_cast<Producer *>(kv.second);
			QRPC_LOGJ(info, {{"ev","try apply answer"},{"mid",mid},{"producer_mid",p->GetRtpParameters().mid}});
			if (p->GetRtpParameters().mid == mid) {
				if (!p->ApplyAnswer(answer, error)) {
					QRPC_LOGJ(error, {{"ev","fail to apply answer"},{"mid",mid},{"error",error}});
					return false;
				}
				found = true;
				break;
			}
		}
		return found;
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
	void Handler::PublishStream(const std::shared_ptr<base::Stream> &stream) {
		published_streams_[stream->label()] = stream;
		router_.Publish(stream.get());
	}
	void Handler::UnpublishStream(const std::shared_ptr<base::Stream> &stream) {
		published_streams_.erase(stream->label());
		for (auto *s : router_.SubscribersFor(stream.get())) {
			s->Close(QRPC_CLOSE_REASON_REMOTE);
		}
		router_.Unpublish(stream.get());
	}
	void Handler::EmitSubscribeStreams(const std::shared_ptr<base::Stream> &stream, const void *p, size_t sz) {
		auto vec = router_.SubscribersFor(stream.get());
		for (auto *s : vec) {
			s->Send(static_cast<const char *>(p), sz);
		}
	}
	bool Handler::SubscribeStream(const std::string &path, const std::shared_ptr<base::Stream> &stream) {
		auto pit = published_streams_.find(path);
		if (pit == published_streams_.end()) {
			QRPC_LOGJ(warn, {{"ev","stream is not published"},{"path", path}});
			ASSERT(false);
			return false;
		}
		return router_.Subscribe(pit->second.get(), stream.get());
	}
	void Handler::UnsubscribeStream(const Stream *stream) {
		router_.Unsubscribe(stream);
	}
}
}