#include "base/rtp/producer.h"
#include "base/rtp/parameters.h"
#include "base/rtp/handler.h"
#include "base/string.h"

namespace base {
namespace rtp {
	bool Producer::consumer_params(const RTC::RtpParameters &consumed_producer_params, Parameters &p) const {
		// just copy mid
		p.mid = params_.mid;
		// choose codecs from consumed_producer_params.codecs that matched params_.codec_capabilities
		std::map<int32_t, const RTC::RtpCodecParameters *> rtxmap;
		for (auto &consumable_codec : consumed_producer_params.codecs) {
			if (consumable_codec.mimeType.subtype == RTC::RtpCodecMimeType::Subtype::RTX) {
				if (consumable_codec.parameters.HasInteger("apt")) {
					auto apt = consumable_codec.parameters.GetInteger("apt");
					rtxmap[apt] = &consumable_codec;
				}
				continue;
			}
			for (auto &receivable_codec : params_.codec_capabilities) {
				if (consumable_codec.mimeType == receivable_codec.mimeType) {
					p.codecs.emplace_back(receivable_codec);
				}
			}
		}
		bool has_rtx = false;
		for (auto &c : p.codecs) {
			auto rtxmit = rtxmap.find(c.payloadType);
			if (rtxmit != rtxmap.end()) {
				p.codecs.emplace_back(*rtxmit->second);
				has_rtx = true;
			}
		}
		// filter target_producer_params.headerExtensions with params_.headerExtensions so that only value and url is matched
		// extensions are remained
		bool transport_wide_cc = false, goog_remb = false;
		for (auto &ext : consumed_producer_params.headerExtensions) {
			for (auto &ext2 : params_.headerExtensions) {
				if (ext.id == ext2.id && ext.type == ext2.type) {
					p.headerExtensions.emplace_back(ext2);
					if (ext2.type == RTC::RtpHeaderExtensionUri::Type::TRANSPORT_WIDE_CC_01) {
						transport_wide_cc = true;
					} else if (!transport_wide_cc && ext2.type == RTC::RtpHeaderExtensionUri::Type::ABS_SEND_TIME) {
						goog_remb = true;
					}
				}
			}
		}
		// after headerExtension fixed, remove unused rtcpfeedback of codec in p.codecs
		if (transport_wide_cc) {
			for (auto &codec : p.codecs) {
				for (auto it = codec.rtcpFeedback.rbegin(); it != codec.rtcpFeedback.rend(); ++it) {
					if (it->type == "goog-remb") {
						codec.rtcpFeedback.erase(std::next(it).base());
					}
				}
			}
		} else if (goog_remb) {
			for (auto &codec : p.codecs) {
				for (auto it = codec.rtcpFeedback.rbegin(); it != codec.rtcpFeedback.rend(); ++it) {
					if (it->type == "transport-cc") {
						codec.rtcpFeedback.erase(std::next(it).base());
					}
				}
			}
		} else {
			for (auto &codec : p.codecs) {
				for (auto it = codec.rtcpFeedback.rbegin(); it != codec.rtcpFeedback.rend(); --it) {
					if (it->type == "goog-remb" || it->type == "transport-cc") {
						codec.rtcpFeedback.erase(std::next(it).base());
					}
				}
			}
		}
		// reduce encoding to just one. if target_producer_params.encodings seems simulcast, set scalabilityMode properly
		// bit rate is maximum one in the encodings
		auto &encoding = p.encodings.emplace_back();
		encoding.ssrc = p.ssrc_seed;

		if (has_rtx) {
			encoding.rtx.ssrc = encoding.ssrc + 1;
			encoding.hasRtx = true;
		}

		// If any of the consumableRtpParameters.encodings has scalabilityMode,
		// process it (assume all encodings have the same value).
		std::string scalability_mode;
		// Use the maximum maxBitrate in any encoding and honor it in the Consumer's
		// encoding.
		uint32_t max_encoding_bitrate = 0;
		for (auto &e : consumed_producer_params.encodings) {
			if (max_encoding_bitrate < e.maxBitrate) {
				max_encoding_bitrate = e.maxBitrate;
			}
			if (!e.scalabilityMode.empty()) {
				scalability_mode = e.scalabilityMode;
			}
		}
		ASSERT(!scalability_mode.empty());
		// If there is simulast, mangle spatial layers in scalabilityMode.
		if (consumed_producer_params.encodings.size() > 1) {
			int temporal_layers = 1;
			try {
				auto pos = scalability_mode.find("T");
				if (pos == std::string::npos) {
					QRPC_LOGJ(error, {{"ev","invalid scalablity mode: no temporal layer spec"},{"scalability_mode",scalability_mode}});
					ASSERT(false);
					return false;					
				}
				temporal_layers = std::stoi(scalability_mode.substr(pos + 1));
			} catch (std::exception &e) {
				QRPC_LOGJ(error, {{"ev","invalid scalablity mode"},{"scalability_mode",scalability_mode},{"error",e.what()}});
				return false;
			}
			QRPC_LOGJ(info, {
				{"ev","replace scalability mode"},
				{"from",scalability_mode},
				{"to",str::Format("L%uT%d", consumed_producer_params.encodings.size(), temporal_layers)}
			});
			scalability_mode = str::Format("L%uT%d", consumed_producer_params.encodings.size(), temporal_layers);
		}
		if (!scalability_mode.empty()) {
			encoding.scalabilityMode = scalability_mode;
		}
		if (max_encoding_bitrate > 0) {
			encoding.maxBitrate = max_encoding_bitrate;
		}
		return true;
	}

	std::string ProducerFactory::GenerateId(const std::string &id, const std::string &label, Parameters::MediaKind kind) { 
		return "/p/" + id + "/" + label + "/" + Parameters::FromMediaKind(kind);
	}
  Producer *ProducerFactory::Create(const std::string &id, const Parameters &p) {
		auto &fbb = Handler::GetFBB();
    auto m = handler_.FindFrom(p);
		try {
			Handler::SetProducerFactory([m, op = p](
				RTC::Shared* shared,
		  	const std::string& id,
		  	RTC::Producer::Listener *listener,
		  	const FBS::Transport::ProduceRequest *req
			) mutable {
				return new Producer(shared, id, listener, op, req, m);
			});
			handler_.HandleRequest(fbb, FBS::Request::Method::TRANSPORT_PRODUCE, p.MakeProduceRequest(fbb, id));
			return handler_.FindProducer(id);
		} catch (std::exception &e) {
			QRPC_LOG(error, "failed to create producer: %s", e.what());
			return nullptr;
		}
  }
}
}