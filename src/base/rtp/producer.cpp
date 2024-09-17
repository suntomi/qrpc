#include "base/rtp/producer.h"
#include "base/rtp/handler.h"

namespace base {
namespace rtp {
  std::shared_ptr<Producer> ProducerFactory::Create(const std::string &id, const Parameters &p) {
    auto m = handler_.FindFrom(p);
    auto data = p.ToJson();
    data["rtpMapping"] = p.ToMapping();
		auto producer = std::make_shared<Producer>(&handler_.shared(), id, &handler_, data);
    producer->SetMedia(m);
    if (!Add(producer)) {
      return nullptr;
    }
    return producer;
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
					return producer;
				}
			}
		}
		return nullptr;
  }
  void ProducerFactory::Remove(std::shared_ptr<Producer> &p) {
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
    producers_.push_back(p);
    auto producer = p.get();

		const auto& rtpParameters = producer->GetRtpParameters();

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
					this->ssrcTable[ssrc] = producer;
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
					this->ssrcTable[ssrc] = producer;
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
				this->midTable[mid] = producer;
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
				this->ridTable[rid] = producer;
			}
			// Just fail if no MID is given.
			else if (rtpParameters.mid.empty())
			{
				Remove(p);

				QRPC_LOG(error, "RID already exists in RTP listener and no MID is given [rid:%s]", rid.c_str());
        return false;
			}
		}
    return true;
  }
}
}