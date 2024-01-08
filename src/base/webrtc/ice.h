#ifndef MS_RTC_ICE_SERVER_HPP
#define MS_RTC_ICE_SERVER_HPP

#include "common.hpp"
#include "RTC/StunPacket.hpp"
#include "base/session.h"
#include "base/alarm.h"
#include <list>
#include <string>

namespace base {
namespace webrtc {
	// IceServer
	class IceServer
	{
	public:
		enum class IceState
		{
			NEW = 1,
			CONNECTED,
			COMPLETED,
			DISCONNECTED
		};

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
		IceServer(Listener* listener, const std::string& usernameFragment, const std::string& password);
		~IceServer();

	public:
		void ProcessStunPacket(RTC::StunPacket* packet, Session *session);
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
		void ForceSetSelectedSession(Session *s) {
			this->SetSelectedSession(s);
		}
		void RestartIce(const std::string& usernameFragment, const std::string& password)
		{
			if (!this->oldUsernameFragment.empty())
			{
				this->listener->OnIceServerLocalUsernameFragmentRemoved(this, this->oldUsernameFragment);
			}

			this->oldUsernameFragment = this->usernameFragment;
			this->usernameFragment    = usernameFragment;

			this->oldPassword = this->password;
			this->password    = password;

			this->remoteNomination = 0u;

			// Notify the listener.
			this->listener->OnIceServerLocalUsernameFragmentAdded(this, usernameFragment);

			// NOTE: Do not call listener->OnIceServerLocalUsernameFragmentRemoved()
			// yet with old usernameFragment. Wait until we receive a STUN packet
			// with the new one.
		}
		bool IsValidTuple(const Session *session) const;
		void RemoveTuple(Session *session);
		/**
		 * This should be just called in 'connected' or 'completed' state and the
		 * given tuple must be an already valid tuple.
		 */
		void MayForceSelectedSession(const Session *session);

	private:
		void HandleTuple(
		  Session *session, bool hasUseCandidate, bool hasNomination, uint32_t nomination);
		/**
		 * Store the given tuple and return its stored address.
		 */
		Session *AddTuple(Session *session);
		/**
		 * If the given tuple exists return its stored address, nullptr otherwise.
		 */
		Session *HasTuple(const Session *session) const;
		/**
		 * Set the given tuple as the selected tuple.
		 * NOTE: The given tuple MUST be already stored within the list.
		 */
		void SetSelectedSession(Session *storedTuple);

	private:
		// Passed by argument.
		Listener* listener{ nullptr };
		// Others.
		std::string usernameFragment;
		std::string password;
		std::string oldUsernameFragment;
		std::string oldPassword;
		IceState state{ IceState::NEW };
		uint32_t remoteNomination{ 0u };
		std::list<Session*> sessions;
		Session *selectedSession{ nullptr };
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
    };
    typedef uint8_t TxId[12];
	public:
		class Listener {
		public:
			virtual void OnIceProberBindingRequest() = 0;
		};
  public:
    IceProber(Listener &l, const std::string &uflag, const std::string &pwd) :
			IceProber(l, uflag, pwd, qrpc_time_sec(5), qrpc_time_sec(10)) {}
    IceProber(Listener &l, const std::string &uflag, const std::string &pwd,
			qrpc_time_t disconnect_timeout, qrpc_time_t failed_timeout) :
      listener_(l), uflag_(uflag), pwd_(pwd), 
			alarm_processor_(NopAlarmProcessor::Instance()), alarm_id_(AlarmProcessor::INVALID_ID),
			disconnect_timeout_(disconnect_timeout), failed_timeout_(failed_timeout) {}
    ~IceProber() { Stop(); }
		inline bool active() const { return state_ != NEW; }
  public:
    qrpc_time_t operator()();
		void Start(AlarmProcessor &ap) {
			if (alarm_id_ == AlarmProcessor::INVALID_ID) {
				alarm_processor_ = ap;
				alarm_id_ = alarm_processor_.Set(*this, qrpc_time_now());
			}
		}
		void Stop() {
			if (alarm_id_ != AlarmProcessor::INVALID_ID) {
				alarm_processor_.Cancel(alarm_id_);
				alarm_id_ = AlarmProcessor::INVALID_ID;
			}
		}
    void Success();
		void SendBindingRequest(Session *s);
  private:
    Listener &listener_;
		std::string uflag_, pwd_;
		AlarmProcessor &alarm_processor_;
		AlarmProcessor::Id alarm_id_;
    State state_{NEW};
    qrpc_time_t last_success_{0ULL};
    qrpc_time_t disconnect_timeout_;
    qrpc_time_t failed_timeout_;
  };
} // namespace webrtc
} // namespace base

#endif
