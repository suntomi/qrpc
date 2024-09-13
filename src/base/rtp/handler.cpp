#include "base/rtp/handler.h"
#include "base/webrtc/mpatch.h"
#include "base/logger.h"
#include "base/string.h"

#include "base/webrtc/mpatch.h"

#include "RTC/BweType.hpp"
#include "RTC/PipeConsumer.hpp"
#include "RTC/RTCP/FeedbackPs.hpp"
#include "RTC/RTCP/FeedbackPsAfb.hpp"
#include "RTC/RTCP/FeedbackPsRemb.hpp"
#include "RTC/RTCP/FeedbackRtp.hpp"
#include "RTC/RTCP/FeedbackRtpNack.hpp"
#include "RTC/RTCP/FeedbackRtpTransport.hpp"
#include "RTC/RTCP/XrDelaySinceLastRr.hpp"
#include "RTC/RtpDictionaries.hpp"
#include "RTC/SimpleConsumer.hpp"
#include "RTC/SimulcastConsumer.hpp"
#include "RTC/SvcConsumer.hpp"
#include "RTC/RtcLogger.hpp"
#include "RTC/TransportCongestionControlClient.hpp"
#include "RTC/TransportCongestionControlServer.hpp"
#include <libwebrtc/modules/rtp_rtcp/include/rtp_rtcp_defines.h> // webrtc::RtpPacketSendInfo

namespace base {
namespace rtp {
	qrpc_time_t Handler::OnTimer(qrpc_time_t now) {
		SendRtcp(qrpc_time_to_msec(now));
		return now + RtcpRandomInterval();
	}
	void Handler::ReceiveRtcpPacket(RTC::RTCP::Packet *packet) {
		MS_TRACE();

		// Handle each RTCP packet.
		while (packet)
		{
			HandleRtcpPacket(packet);

			auto* previousPacket = packet;

			packet = packet->GetNext();

			delete previousPacket;
		}
	}
	void Handler::SendRtcp(uint64_t nowMs)
	{
		MS_TRACE();

		std::unique_ptr<RTC::RTCP::CompoundPacket> packet{ new RTC::RTCP::CompoundPacket() };

		for (auto& kv : this->mapConsumers)
		{
			auto* consumer = kv.second;
			auto rtcpAdded = consumer->GetRtcp(packet.get(), nowMs);

			// RTCP data couldn't be added because the Compound packet is full.
			// Send the RTCP compound packet and request for RTCP again.
			if (!rtcpAdded)
			{
				listener_.SendRtcpCompoundPacket(packet.get());

				// Create a new compount packet.
				packet.reset(new RTC::RTCP::CompoundPacket());

				// Retrieve the RTCP again.
				consumer->GetRtcp(packet.get(), nowMs);
			}
		}

		for (auto& kv : this->mapProducers)
		{
			auto* producer = kv.second;
			auto rtcpAdded = producer->GetRtcp(packet.get(), nowMs);

			// RTCP data couldn't be added because the Compound packet is full.
			// Send the RTCP compound packet and request for RTCP again.
			if (!rtcpAdded)
			{
				listener_.SendRtcpCompoundPacket(packet.get());

				// Create a new compount packet.
				packet.reset(new RTC::RTCP::CompoundPacket());

				// Retrieve the RTCP again.
				producer->GetRtcp(packet.get(), nowMs);
			}
		}

		// Send the RTCP compound packet if there is any sender or receiver report.
		if (packet->GetReceiverReportCount() > 0u || packet->GetSenderReportCount() > 0u)
		{
			listener_.SendRtcpCompoundPacket(packet.get());
		}
	}
	// TODO: how to get ssrc of consumer, that usually not contained in SDP?
	inline RTC::Consumer* Handler::GetConsumerByMediaSsrc(uint32_t ssrc) const {
		MS_TRACE();
		auto mapSsrcConsumerIt = this->mapSsrcConsumer.find(ssrc);
		if (mapSsrcConsumerIt == this->mapSsrcConsumer.end()) {
			return nullptr;
		}
		auto* consumer = mapSsrcConsumerIt->second;
		return consumer;
	}
	// TODO: how to get rtx ssrc of consumer, that usually not contained in SDP?
	inline RTC::Consumer* Handler::GetConsumerByRtxSsrc(uint32_t ssrc) const {
		MS_TRACE();
		auto mapRtxSsrcConsumerIt = this->mapRtxSsrcConsumer.find(ssrc);
		if (mapRtxSsrcConsumerIt == this->mapRtxSsrcConsumer.end()) {
			return nullptr;
		}
		auto* consumer = mapRtxSsrcConsumerIt->second;
		return consumer;
	}	
	void Handler::HandleRtcpPacket(RTC::RTCP::Packet *packet) {
		MS_TRACE();

		switch (packet->GetType())
		{
			case RTC::RTCP::Type::RR:
			{
				auto* rr = static_cast<RTC::RTCP::ReceiverReportPacket*>(packet);

				for (auto it = rr->Begin(); it != rr->End(); ++it)
				{
					auto& report   = *it;
					auto* consumer = GetConsumerByMediaSsrc(report->GetSsrc());

					if (!consumer)
					{
						// Special case for the RTP probator.
						if (report->GetSsrc() == RTC::RtpProbationSsrc)
						{
							continue;
						}

						// Special case for (unused) RTCP-RR from the RTX stream.
						if (GetConsumerByRtxSsrc(report->GetSsrc()) != nullptr)
						{
							continue;
						}

						MS_DEBUG_TAG(
						  rtcp,
						  "no Consumer found for received Receiver Report [ssrc:%" PRIu32 "]",
						  report->GetSsrc());

						continue;
					}

					consumer->ReceiveRtcpReceiverReport(report);
				}

				if (this->tccClient && !this->mapConsumers.empty())
				{
					float rtt = 0;

					// Retrieve the RTT from the first active consumer.
					for (auto& kv : this->mapConsumers)
					{
						auto* consumer = kv.second;

						if (consumer->IsActive())
						{
							rtt = consumer->GetRtt();

							break;
						}
					}

					this->tccClient->ReceiveRtcpReceiverReport(rr, rtt, DepLibUV::GetTimeMsInt64());
				}

				break;
			}

			case RTC::RTCP::Type::PSFB:
			{
				auto* feedback = static_cast<RTC::RTCP::FeedbackPsPacket*>(packet);

				switch (feedback->GetMessageType())
				{
					case RTC::RTCP::FeedbackPs::MessageType::PLI:
					{
						auto* consumer = GetConsumerByMediaSsrc(feedback->GetMediaSsrc());

						if (feedback->GetMediaSsrc() == RTC::RtpProbationSsrc)
						{
							break;
						}
						else if (!consumer)
						{
							MS_DEBUG_TAG(
							  rtcp,
							  "no Consumer found for received PLI Feedback packet "
							  "[sender ssrc:%" PRIu32 ", media ssrc:%" PRIu32 "]",
							  feedback->GetSenderSsrc(),
							  feedback->GetMediaSsrc());

							break;
						}

						MS_DEBUG_TAG(
						  rtcp,
						  "PLI received, requesting key frame for Consumer "
						  "[sender ssrc:%" PRIu32 ", media ssrc:%" PRIu32 "]",
						  feedback->GetSenderSsrc(),
						  feedback->GetMediaSsrc());

						consumer->ReceiveKeyFrameRequest(
						  RTC::RTCP::FeedbackPs::MessageType::PLI, feedback->GetMediaSsrc());

						break;
					}

					case RTC::RTCP::FeedbackPs::MessageType::FIR:
					{
						// Must iterate FIR items.
						auto* fir = static_cast<RTC::RTCP::FeedbackPsFirPacket*>(packet);

						for (auto it = fir->Begin(); it != fir->End(); ++it)
						{
							auto& item     = *it;
							auto* consumer = GetConsumerByMediaSsrc(item->GetSsrc());

							if (item->GetSsrc() == RTC::RtpProbationSsrc)
							{
								continue;
							}
							else if (!consumer)
							{
								MS_DEBUG_TAG(
								  rtcp,
								  "no Consumer found for received FIR Feedback packet "
								  "[sender ssrc:%" PRIu32 ", media ssrc:%" PRIu32 ", item ssrc:%" PRIu32 "]",
								  feedback->GetSenderSsrc(),
								  feedback->GetMediaSsrc(),
								  item->GetSsrc());

								continue;
							}

							MS_DEBUG_TAG(
							  rtcp,
							  "FIR received, requesting key frame for Consumer "
							  "[sender ssrc:%" PRIu32 ", media ssrc:%" PRIu32 ", item ssrc:%" PRIu32 "]",
							  feedback->GetSenderSsrc(),
							  feedback->GetMediaSsrc(),
							  item->GetSsrc());

							consumer->ReceiveKeyFrameRequest(feedback->GetMessageType(), item->GetSsrc());
						}

						break;
					}

					case RTC::RTCP::FeedbackPs::MessageType::AFB:
					{
						auto* afb = static_cast<RTC::RTCP::FeedbackPsAfbPacket*>(feedback);

						// Store REMB info.
						if (afb->GetApplication() == RTC::RTCP::FeedbackPsAfbPacket::Application::REMB)
						{
							auto* remb = static_cast<RTC::RTCP::FeedbackPsRembPacket*>(afb);

							// Pass it to the TCC client.
							// clang-format off
							if (
								this->tccClient &&
								this->tccClient->GetBweType() == RTC::BweType::REMB
							)
							// clang-format on
							{
								this->tccClient->ReceiveEstimatedBitrate(remb->GetBitrate());
							}

							break;
						}
						else
						{
							MS_DEBUG_TAG(
							  rtcp,
							  "ignoring unsupported %s Feedback PS AFB packet "
							  "[sender ssrc:%" PRIu32 ", media ssrc:%" PRIu32 "]",
							  RTC::RTCP::FeedbackPsPacket::MessageType2String(feedback->GetMessageType()).c_str(),
							  feedback->GetSenderSsrc(),
							  feedback->GetMediaSsrc());

							break;
						}
					}

					default:
					{
						MS_DEBUG_TAG(
						  rtcp,
						  "ignoring unsupported %s Feedback packet "
						  "[sender ssrc:%" PRIu32 ", media ssrc:%" PRIu32 "]",
						  RTC::RTCP::FeedbackPsPacket::MessageType2String(feedback->GetMessageType()).c_str(),
						  feedback->GetSenderSsrc(),
						  feedback->GetMediaSsrc());
					}
				}

				break;
			}

			case RTC::RTCP::Type::RTPFB:
			{
				auto* feedback = static_cast<RTC::RTCP::FeedbackRtpPacket*>(packet);
				auto* consumer = GetConsumerByMediaSsrc(feedback->GetMediaSsrc());

				// If no Consumer is found and this is not a Transport Feedback for the
				// probation SSRC or any Consumer RTX SSRC, ignore it.
				//
				// clang-format off
				if (
					!consumer &&
					feedback->GetMessageType() != RTC::RTCP::FeedbackRtp::MessageType::TCC &&
					(
						feedback->GetMediaSsrc() != RTC::RtpProbationSsrc ||
						!GetConsumerByRtxSsrc(feedback->GetMediaSsrc())
					)
				)
				// clang-format on
				{
					MS_DEBUG_TAG(
					  rtcp,
					  "no Consumer found for received Feedback packet "
					  "[sender ssrc:%" PRIu32 ", media ssrc:%" PRIu32 "]",
					  feedback->GetSenderSsrc(),
					  feedback->GetMediaSsrc());

					break;
				}

				switch (feedback->GetMessageType())
				{
					case RTC::RTCP::FeedbackRtp::MessageType::NACK:
					{
						if (!consumer)
						{
							MS_DEBUG_TAG(
							  rtcp,
							  "no Consumer found for received NACK Feedback packet "
							  "[sender ssrc:%" PRIu32 ", media ssrc:%" PRIu32 "]",
							  feedback->GetSenderSsrc(),
							  feedback->GetMediaSsrc());

							break;
						}

						auto* nackPacket = static_cast<RTC::RTCP::FeedbackRtpNackPacket*>(packet);

						consumer->ReceiveNack(nackPacket);

						break;
					}

					case RTC::RTCP::FeedbackRtp::MessageType::TCC:
					{
						auto* feedback = static_cast<RTC::RTCP::FeedbackRtpTransportPacket*>(packet);

						if (this->tccClient)
						{
							this->tccClient->ReceiveRtcpTransportFeedback(feedback);
						}

#ifdef ENABLE_RTC_SENDER_BANDWIDTH_ESTIMATOR
						// Pass it to the SenderBandwidthEstimator client.
						if (this->senderBwe)
						{
							this->senderBwe->ReceiveRtcpTransportFeedback(feedback);
						}
#endif

						break;
					}

					default:
					{
						MS_DEBUG_TAG(
						  rtcp,
						  "ignoring unsupported %s Feedback packet "
						  "[sender ssrc:%" PRIu32 ", media ssrc:%" PRIu32 "]",
						  RTC::RTCP::FeedbackRtpPacket::MessageType2String(feedback->GetMessageType()).c_str(),
						  feedback->GetSenderSsrc(),
						  feedback->GetMediaSsrc());
					}
				}

				break;
			}

			case RTC::RTCP::Type::SR:
			{
				auto* sr = static_cast<RTC::RTCP::SenderReportPacket*>(packet);

				// Even if Sender Report packet can only contains one report.
				for (auto it = sr->Begin(); it != sr->End(); ++it)
				{
					auto& report   = *it;
					auto* producer = producer_factory_.Get(report->GetSsrc());

					if (!producer)
					{
						MS_DEBUG_TAG(
						  rtcp,
						  "no Producer found for received Sender Report [ssrc:%" PRIu32 "]",
						  report->GetSsrc());

						continue;
					}

					producer->ReceiveRtcpSenderReport(report);
				}

				break;
			}

			case RTC::RTCP::Type::SDES:
			{
				// According to RFC 3550 section 6.1 "a CNAME item MUST be included in
				// each compound RTCP packet". So this is true even for compound
				// packets sent by endpoints that are not sending any RTP stream to us
				// (thus chunks in such a SDES will have an SSCR does not match with
				// any Producer created in this Transport).
				// Therefore, and given that we do nothing with SDES, just ignore them.

				break;
			}

			case RTC::RTCP::Type::BYE:
			{
				MS_DEBUG_TAG(rtcp, "ignoring received RTCP BYE");

				break;
			}

			case RTC::RTCP::Type::XR:
			{
				auto* xr = static_cast<RTC::RTCP::ExtendedReportPacket*>(packet);

				for (auto it = xr->Begin(); it != xr->End(); ++it)
				{
					auto& report = *it;

					switch (report->GetType())
					{
						case RTC::RTCP::ExtendedReportBlock::Type::DLRR:
						{
							auto* dlrr = static_cast<RTC::RTCP::DelaySinceLastRr*>(report);

							for (auto it2 = dlrr->Begin(); it2 != dlrr->End(); ++it2)
							{
								auto& ssrcInfo = *it2;

								// SSRC should be filled in the sub-block.
								if (ssrcInfo->GetSsrc() == 0)
								{
									ssrcInfo->SetSsrc(xr->GetSsrc());
								}

								auto* producer = producer_factory_.Get(ssrcInfo->GetSsrc());

								if (!producer)
								{
									MS_DEBUG_TAG(
									  rtcp,
									  "no Producer found for received Sender Extended Report [ssrc:%" PRIu32 "]",
									  ssrcInfo->GetSsrc());

									continue;
								}

								producer->ReceiveRtcpXrDelaySinceLastRr(ssrcInfo);
							}

							break;
						}

						case RTC::RTCP::ExtendedReportBlock::Type::RRT:
						{
							auto* rrt = static_cast<RTC::RTCP::ReceiverReferenceTime*>(report);

							for (auto& kv : this->mapConsumers)
							{
								auto* consumer = kv.second;

								consumer->ReceiveRtcpXrReceiverReferenceTime(rrt);
							}

							break;
						}

						default:;
					}
				}

				break;
			}

			default:
			{
				MS_DEBUG_TAG(
				  rtcp,
				  "unhandled RTCP type received [type:%" PRIu8 "]",
				  static_cast<uint8_t>(packet->GetType()));
			}
		}
	}
	void Handler::SetNegotiationArgs(const std::map<std::string, json> &args) {
		const auto rlmit = args.find("ridLabelMap");
		if (rlmit != args.end()) {
			rid_label_map_ = rlmit->second.get<std::map<std::string,std::string>>();
			QRPC_LOGJ(info, {{"ev","new rid label map"},{"map",*rlmit}});
		}
		const auto tlmit = args.find("trackIdLabelMap");
		if (tlmit != args.end()) {
			trackid_label_map_ = tlmit->second.get<std::map<std::string,std::string>>();
			QRPC_LOGJ(info, {{"ev","new track id label map"},{"map",*tlmit}});
		}
		const auto rsmit = args.find("ridScalabilityModeMap");
		if (rsmit != args.end()) {
			rid_scalability_mode_map_ = rsmit->second.get<std::map<std::string,Media::ScalabilityMode>>();
			QRPC_LOGJ(info, {{"ev","new rid scalability mode map"},{"map",*rsmit}});
		}
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
	int Handler::CreateProducer(const std::string &id, const Parameters &p) {
		for (auto kv : p.ssrcs) {
			ssrc_trackid_map_[kv.first] = kv.second.track_id;
		}
		auto producer = producer_factory_.Create(id, p);
		return producer != nullptr ? QRPC_OK : QRPC_EINVAL;
	}
	int Handler::CreateConsumer(const Parameters &p) {
		return QRPC_OK;
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
  void Handler::ReceiveRtpPacket(const std::string &id, RTC::RtpPacket &packet) {
    MS_TRACE();

		packet.logger.recvTransportId = id;

		// Apply the Transport RTP header extension ids so the RTP listener can use them.
		packet.SetMidExtensionId(ext_ids().mid);
		packet.SetRidExtensionId(ext_ids().rid);
		packet.SetRepairedRidExtensionId(ext_ids().rrid);
		packet.SetAbsSendTimeExtensionId(ext_ids().absSendTime);
		packet.SetTransportWideCc01ExtensionId(ext_ids().transportWideCc01);

		auto nowMs = qrpc_time_to_msec(qrpc_time_now());

		// Feed the TransportCongestionControlServer.
		if (this->tccServer)
		{
			this->tccServer->IncomingPacket(nowMs, &packet);
		}

		// std::string rid, mid;
		// packet.ReadMid(mid);
		// packet.ReadRid(rid);
		// QRPC_LOGJ(debug, {
    //   {"ev","RTP packet received"},
    //   {"proto","srtp"},{"ssrc",packet.GetSsrc()},{"rid",rid},{"mid",mid},
    //   {"payloadType",packet.GetPayloadType()},{"seq",packet.GetSequenceNumber()}
    // });

		// Get the associated Producer.
		RTC::Producer* producer = producer_factory_.Get(packet);
		
		if (!producer) {
			packet.logger.Dropped(RTC::RtcLogger::RtpPacket::DropReason::PRODUCER_NOT_FOUND);
			MS_WARN_TAG(
			  rtp,
			  "no suitable Producer for received RTP packet [ssrc:%" PRIu32 ", payloadType:%" PRIu8 "]",
			  packet.GetSsrc(),
			  packet.GetPayloadType());
			// Tell the child class to remove this SSRC.
			listener_.RecvStreamClosed(packet.GetSsrc());
			return;
		}

		QRPC_LOG(error, 
		  "RTP packet received [ssrc:%" PRIu32 ", payloadType:%" PRIu8 ", producerId:%s]",
		  packet.GetSsrc(),
		  packet.GetPayloadType(),
		 id.c_str());

		// Pass the RTP packet to the corresponding Producer.
		auto result = producer->ReceiveRtpPacket(&packet);

		switch (result)
		{
			case RTC::Producer::ReceiveRtpPacketResult::MEDIA:
				this->recvRtpTransmission.Update(&packet);
				break;
			case RTC::Producer::ReceiveRtpPacketResult::RETRANSMISSION:
				this->recvRtxTransmission.Update(&packet);
				break;
			case RTC::Producer::ReceiveRtpPacketResult::DISCARDED:
				// Tell the child class to remove this SSRC.
				listener_.RecvStreamClosed(packet.GetSsrc());
				break;
			default:;
		}
  }
}
}