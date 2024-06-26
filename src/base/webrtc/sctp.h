#ifndef MS_RTC_SCTP_ASSOCIATION_HPP
#define MS_RTC_SCTP_ASSOCIATION_HPP

#include "common.hpp"
#include "Utils.hpp"

#include "base/alarm.h"
#include "base/stream.h"

#include <usrsctp.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace base {
namespace webrtc {  
	class SctpAssociation
	{
	public:
		enum class SctpState
		{
			NEW = 1,
			CONNECTING,
			CONNECTED,
			FAILED,
			CLOSED
		};

	private:
		enum class StreamDirection
		{
			INCOMING = 1,
			OUTGOING
		};

	public:
		class Listener
		{
		public:
			virtual ~Listener() = default;

		public:
			virtual void OnSctpAssociationConnecting(SctpAssociation* sctpAssociation) = 0;
			virtual void OnSctpAssociationConnected(SctpAssociation* sctpAssociation)  = 0;
			virtual void OnSctpAssociationFailed(SctpAssociation* sctpAssociation)     = 0;
			virtual void OnSctpAssociationClosed(SctpAssociation* sctpAssociation)     = 0;
			virtual void OnSctpAssociationSendData(
			  SctpAssociation* sctpAssociation, const uint8_t* data, size_t len) = 0;
			virtual void OnSctpStreamReset(
			  SctpAssociation* sctpAssociation, uint16_t streamId) = 0;
			virtual void OnSctpWebRtcDataChannelControlDataReceived(
			  SctpAssociation* sctpAssociation,
			  uint16_t streamId,
			  const uint8_t* msg,
			  size_t len) = 0;
			virtual void OnSctpAssociationMessageReceived(
			  SctpAssociation* sctpAssociation,
			  uint16_t streamId,
			  uint32_t ppid,
			  const uint8_t* msg,
			  size_t len) = 0;
			virtual void OnSctpAssociationBufferedAmount(
			  SctpAssociation* sctpAssociation, uint32_t len) = 0;
		};

	public:
		static bool IsSctp(const uint8_t* data, size_t len)
		{
			// clang-format off
			return (
				(len >= 12) &&
				// Must have Source Port Number and Destination Port Number set to 5000 (hack).
				(Utils::Byte::Get2Bytes(data, 0) == 5000) &&
				(Utils::Byte::Get2Bytes(data, 2) == 5000)
			);
			// clang-format on
		}

	public:
		SctpAssociation(
		  Listener* listener,
		  uint16_t os,
		  uint16_t mis,
		  size_t maxSctpMessageSize,
		  size_t sctpSendBufferSize,
		  bool isDataChannel);
		~SctpAssociation();

	public:
		void FillJson(json& jsonObject) const;
		void TransportConnected();
		SctpState GetState() const
		{
			return this->state;
		}
		size_t GetSctpBufferedAmount() const
		{
			return this->sctpBufferedAmount;
		}
		void ProcessSctpData(const uint8_t* data, size_t len);
		int SendSctpMessage(
		  Stream* dataConsumer,
		  uint32_t ppid,
		  const uint8_t* msg,
		  size_t len);
		void HandleDataConsumer(Stream* dataConsumer);
		void DataConsumerClosed(Stream* dataConsumer);

	private:
		void ResetSctpStream(uint16_t streamId, StreamDirection);
		void AddOutgoingStreams(bool force = false);

		/* Callbacks fired by usrsctp events. */
	public:
		void OnUsrSctpSendSctpData(void* buffer, size_t len);
		void OnUsrSctpReceiveSctpData(
		  uint16_t streamId, uint16_t ssn, uint32_t ppid, int flags, const uint8_t* data, size_t len);
		void OnUsrSctpReceiveSctpNotification(union sctp_notification* notification, size_t len);
		void OnUsrSctpSentData(uint32_t freeBuffer);

	public:
		uintptr_t id{ 0u };

	private:
		// Passed by argument.
		Listener* listener{ nullptr };
		uint16_t os{ 1024u };
		uint16_t mis{ 1024u };
		size_t maxSctpMessageSize{ 262144u };
		size_t sctpSendBufferSize{ 262144u };
		size_t sctpBufferedAmount{ 0u };
		bool isDataChannel{ false };
		// Allocated by this.
		uint8_t* messageBuffer{ nullptr };
		// Others.
		SctpState state{ SctpState::NEW };
		struct socket* socket{ nullptr };
		uint16_t desiredOs{ 0u };
		size_t messageBufferLen{ 0u };
		uint16_t lastSsnReceived{ 0u }; // Valid for us since no SCTP I-DATA support.
	};

  class DepUsrSCTP
  {
  public:
    static void ClassInit(AlarmProcessor &a);
    static void ClassDestroy();
    static void CreateChecker();
    static void CloseChecker();
    static uintptr_t GetNextSctpAssociationId();
    static void RegisterSctpAssociation(SctpAssociation* sctpAssociation);
    static void DeregisterSctpAssociation(SctpAssociation* sctpAssociation);
    static SctpAssociation* RetrieveSctpAssociation(uintptr_t id);

  private:
    static class Timer {
    public:
      Timer() : lastCalledAtMs(0) {}
      qrpc_time_t operator()();
    private:
      int lastCalledAtMs;
    } timer;
    static uint64_t numSctpAssociations;
    static uintptr_t nextSctpAssociationId;
    static std::unordered_map<uintptr_t, SctpAssociation*> mapIdSctpAssociation;
  };  
} // namespace webrtc
} // namespace base

#endif
