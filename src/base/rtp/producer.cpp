#include "base/rtp/producer.h"
#include "base/rtp/parameters.h"
#include "base/rtp/handler.h"
#include "base/string.h"

namespace base {
namespace rtp {
	bool Producer::consumer_params(const RTC::RtpParameters &consumed_producer_params, RTC::RtpParameters &p) const {
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
		encoding.ssrc = Parameters::GenerateSsrc();

		if (has_rtx) {
			encoding.rtx.ssrc = encoding.ssrc + 1;
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
  std::shared_ptr<Producer> ProducerFactory::Create(const std::string &id, const Parameters &p) {
    auto m = handler_.FindFrom(p);
		try {
			auto producer = std::make_shared<Producer>(&handler_.shared(), id, &handler_, p, m);
			if (!Add(producer)) {
				return nullptr;
			}
			return producer;
		} catch (std::exception &e) {
			QRPC_LOG(error, "failed to create producer: %s", e.what());
			return nullptr;
		}
  }
  Producer *ProducerFactory::Get(const RTC::RtpPacket &packet) {
		// First lookup into the SSRC table.
		{
			auto it = this->ssrcTable.find(packet.GetSsrc());
			if (it != this->ssrcTable.end()) {
				auto producer = it->second;
				return producer;
			}
		}
		// Otherwise lookup into the MID table.
		{
			std::string mid;
			if (packet.ReadMid(mid)) {
				auto it = this->midTable.find(mid);
				if (it != this->midTable.end()) {
					auto producer = it->second;
					// Fill the ssrc table.
					// NOTE: We may be overriding an exiting SSRC here, but we don't care.
					this->ssrcTable[packet.GetSsrc()] = producer;
					QRPC_LOGJ(debug, {{"ev","add to ssrc by mid"},{"mid",mid},{"ssrc",packet.GetSsrc()}});
					return producer;
				}
			}
		}
		// Otherwise lookup into the RID table.
		{
			std::string rid;
			if (packet.ReadRid(rid)) {
				auto it = this->ridTable.find(rid);
				if (it != this->ridTable.end()) {
					auto producer = it->second;
					// Fill the ssrc table.
					// NOTE: We may be overriding an exiting SSRC here, but we don't care.
					this->ssrcTable[packet.GetSsrc()] = producer;
					QRPC_LOGJ(debug, {{"ev","add to ssrc by rid"},{"rid",rid},{"ssrc",packet.GetSsrc()}});
					return producer;
				}
			}
		}
		return nullptr;
  }
  void ProducerFactory::Remove(std::shared_ptr<Producer> &p) {
		producers_.erase(p->id);
    auto *producer = p.get();
		// Remove from the listener tables all entries pointing to the Producer.
		for (auto it = this->ssrcTable.begin(); it != this->ssrcTable.end();) {
			if (it->second == producer)
				it = this->ssrcTable.erase(it);
			else
				++it;
		}

		for (auto it = this->midTable.begin(); it != this->midTable.end();) {
			if (it->second == producer)
				it = this->midTable.erase(it);
			else
				++it;
		}

		for (auto it = this->ridTable.begin(); it != this->ridTable.end();) {
			if (it->second == producer)
				it = this->ridTable.erase(it);
			else
				++it;
		}    
  }
  bool ProducerFactory::Add(std::shared_ptr<Producer> &p) {
    producers_[p->id] = p;
    auto producer = p.get();

		const auto& rtpParameters = producer->GetRtpParameters();

		int added = 0;
		// Add entries into the ssrcTable.
		for (const auto& encoding : rtpParameters.encodings)
		{
			uint32_t ssrc;

			// Check encoding.ssrc.
			ssrc = encoding.ssrc;

			if (ssrc != 0u)
			{
				if (this->ssrcTable.find(ssrc) == this->ssrcTable.end())
				{
					QRPC_LOGJ(debug, {{"ev","add to ssrc table"},{"ssrc",ssrc},{"producer",producer->id}});
					this->ssrcTable[ssrc] = producer;
					added++;
				}
				else
				{
					Remove(p);

					QRPC_LOG(error, "ssrc already exists in RTP listener [ssrc:%" PRIu32 "]", ssrc);
          return false;
				}
			}

			// Check encoding.rtx.ssrc.
			ssrc = encoding.rtx.ssrc;

			if (ssrc != 0u)
			{
				if (this->ssrcTable.find(ssrc) == this->ssrcTable.end())
				{
					QRPC_LOGJ(debug, {{"ev","add to ssrc table as rtx"},{"ssrc",ssrc},{"producer",producer->id}});
					this->ssrcTable[ssrc] = producer;
					added++;
				}
				else
				{
					Remove(p);

					QRPC_LOG(error, "RTX ssrc already exists in RTP listener [ssrc:%" PRIu32 "]", ssrc);
          return false;
				}
			}
		}

		// Add entries into midTable.
		if (!rtpParameters.mid.empty())
		{
			const auto& mid = rtpParameters.mid;

			if (this->midTable.find(mid) == this->midTable.end())
			{
				QRPC_LOGJ(debug, {{"ev","add to mid table"},{"mid",mid},{"producer",producer->id}});
				this->midTable[mid] = producer;
				added++;
			}
			else
			{
				Remove(p);

				QRPC_LOG(error, "MID already exists in RTP listener [mid:%s]", mid.c_str());
        return false;
			}
		}

		// Add entries into ridTable.
		for (const auto& encoding : rtpParameters.encodings)
		{
			const auto& rid = encoding.rid;

			if (rid.empty())
				continue;

			if (this->ridTable.find(rid) == this->ridTable.end())
			{
				QRPC_LOGJ(debug, {{"ev","add to rid table"},{"rid",rid},{"producer",producer->id}});
				this->ridTable[rid] = producer;
				added++;
			}
			// Just fail if no MID is given.
			else if (rtpParameters.mid.empty())
			{
				Remove(p);

				QRPC_LOG(error, "RID already exists in RTP listener and no MID is given [rid:%s]", rid.c_str());
        return false;
			}
		}
		ASSERT(added > 0);
    return true;
  }
}
}