#ifndef MS_RTC_ICE_SERVER_HPP
#define MS_RTC_ICE_SERVER_HPP

#include "common.hpp"
#include "FBS/webRtcTransport.h"
#include "RTC/StunPacket.hpp"
#include "handles/TimerHandle.hpp"
#include "base/session.h"
#include "base/alarm.h"
#include <list>
#include <string>

namespace base {
namespace webrtc {
	// IceServer
	class IceServer : public TimerHandle::Listener
	{
	public:
		enum class IceState
		{
			NEW = 1,
			CONNECTED,
			COMPLETED,
			DISCONNECTED,
		};

	public:
		static IceState RoleFromFbs(FBS::WebRtcTransport::IceState state);
		static FBS::WebRtcTransport::IceState IceStateToFbs(IceState state);

	public:
		class Listener
		{
		public:
			virtual ~Listener() = default;

		public:
			/**
			 * These callbacks are guaranteed to be called before ProcessStunPacket()
			 * returns, so the given pointers are still usable.
			 */
			virtual void OnIceServerSendStunPacket(
			  const IceServer *iceServer, const RTC::StunPacket* packet, Session *session) = 0;
			virtual void OnIceServerLocalUsernameFragmentAdded(
			  const IceServer *iceServer, const std::string& usernameFragment) = 0;
			virtual void OnIceServerLocalUsernameFragmentRemoved(
			  const IceServer *iceServer, const std::string& usernameFragment) = 0;
			virtual void OnIceServerSessionAdded(const IceServer *iceServer, Session *session) = 0;
			virtual void OnIceServerSessionRemoved(
			  const IceServer *iceServer, Session *session) = 0;
			virtual void OnIceServerSelectedSession(
			  const IceServer *iceServer, Session *session)        = 0;
			virtual void OnIceServerConnected(const IceServer *iceServer)    = 0;
			virtual void OnIceServerCompleted(const IceServer *iceServer)    = 0;
			virtual void OnIceServerDisconnected(const IceServer *iceServer) = 0;
			virtual bool OnIceServerCheckClosed(const IceServer *iceServer) = 0;
			virtual void OnIceServerSuccessResponded(
					const IceServer *iceServer, const RTC::StunPacket* packet, Session *session) = 0;
			virtual void OnIceServerErrorResponded(
				const IceServer *iceServer, const RTC::StunPacket* packet, Session *session) = 0;
		};

	public:
		IceServer(
		  Listener* listener,
		  const std::string& usernameFragment,
		  const std::string& password,
		  uint8_t consentTimeoutSec);
		~IceServer() override;

	public:
		void ProcessStunPacket(RTC::StunPacket* packet, Session *session);
		std::list<Session*> &GetSessions() { return this->sessions; }
		const std::string& GetUsernameFragment() const
		{
			return this->usernameFragment;
		}
		const std::string& GetPassword() const
		{
			return this->password;
		}
		IceState GetState() const
		{
			return this->state;
		}
		Session *GetSelectedSession() const
		{
			return this->selectedSession;
		}
		void RestartIce(const std::string& usernameFragment, const std::string& password);
		bool ValidatePacket(RTC::StunPacket &packet) const;
		bool IsValidSession(const Session *session) const;
		void RemoveSession(Session *session);
		/**
		 * This should be just called in 'connected' or 'completed' state and the
		 * given tuple must be an already valid tuple.
		 */
		void MayForceSelectedSession(const Session *session);

	private:
		void ProcessStunRequest(RTC::StunPacket* request, Session *session);
		void ProcessStunIndication(RTC::StunPacket* indication);
		void ProcessStunResponse(RTC::StunPacket* response, Session *session);
		void HandleSession(
		  Session *session, bool hasUseCandidate, bool hasNomination, uint32_t nomination);
		/**
		 * Store the given tuple and return its stored address.
		 */
		Session *AddSession(Session *session);
		/**
		 * If the given tuple exists return its stored address, nullptr otherwise.
		 */
		Session *HasSession(const Session *session) const;
		/**
		 * Set the given tuple as the selected tuple.
		 * NOTE: The given tuple MUST be already stored within the list.
		 */
		void SetSelectedSession(Session *storedSession);
		bool IsConsentCheckSupported() const
		{
			return this->consentTimeoutMs != 0u;
		}
		bool IsConsentCheckRunning() const
		{
			return (this->consentCheckTimer && this->consentCheckTimer->IsActive());
		}
		void StartConsentCheck();
		void RestartConsentCheck();
		void StopConsentCheck();

		/* Pure virtual methods inherited from TimerHandle::Listener. */
	public:
		void OnTimer(TimerHandle* timer) override;

	private:
		// Passed by argument.
		Listener* listener{ nullptr };
		std::string usernameFragment;
		std::string password;
		uint16_t consentTimeoutMs{ 30000u };
		// Others.
		std::string oldUsernameFragment;
		std::string oldPassword;
		IceState state{ IceState::NEW };
		uint32_t remoteNomination{ 0u };
		std::list<Session*> sessions;
		Session *selectedSession{ nullptr };
		TimerHandle* consentCheckTimer{ nullptr };
		uint64_t lastConsentRequestReceivedAtMs{ 0u };
		bool isRemovingSessions{ false };
	};

	// IceProber
	class IceProber {
  public:
    enum State {
      NEW = 0,
      CONNECTED,
      CHECKING,
      DISCONNECTED,
      FAILED,
			STOPPED,
    };
    typedef uint8_t TxId[12];
  public:
    IceProber(const std::string &ufrag, const std::string &pwd, uint64_t priority) :
			IceProber(ufrag, pwd, priority, qrpc_time_sec(5), qrpc_time_sec(10)) {}
    IceProber(const std::string &ufrag, const std::string &pwd, uint64_t priority,
			qrpc_time_t disconnect_timeout, qrpc_time_t failed_timeout) :
      ufrag_(ufrag), pwd_(pwd), priority_(priority),
			disconnect_timeout_(disconnect_timeout), failed_timeout_(failed_timeout) {}
    ~IceProber() {}
		inline bool active() const { return state_ != NEW; }
  public:
    qrpc_time_t OnTimer(Session *s);
    void Success();
		void Reset() { state_ = NEW; last_success_ = 0; }
		void SendBindingRequest(Session *s);
  private:
		std::string ufrag_, pwd_;
		uint64_t priority_;
    State state_{NEW};
    qrpc_time_t last_success_{0ULL};
    qrpc_time_t disconnect_timeout_;
    qrpc_time_t failed_timeout_;
  };
} // namespace webrtc
} // namespace base

#endif
