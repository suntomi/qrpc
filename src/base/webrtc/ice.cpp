#define MS_CLASS "RTC::IceServer"
// #define MS_LOG_DEV_LEVEL 3

#include <utility>

#include "base/logger.h"
#include "base/webrtc/ice.h"

// need to put last for overriding MS_XXX macro (because Logger.hpp also undef MS_XXX macro)
#define QRPC_DISABLE_MS_TRACK
#include "base/webrtc/mpatch.h"

namespace base {
namespace webrtc {
	/* Static. */

	static constexpr size_t StunSerializeBufferSize{ 65536 };
	thread_local static uint8_t StunSerializeBuffer[StunSerializeBufferSize];
	static constexpr size_t MaxSessions{ 8 };
	static constexpr uint8_t ConsentCheckMinTimeoutSec{ 10u };
	static constexpr uint8_t ConsentCheckMaxTimeoutSec{ 60u };

	/* Class methods. */
	IceServer::IceState IceStateFromFbs(FBS::WebRtcTransport::IceState state)
	{
		switch (state)
		{
			case FBS::WebRtcTransport::IceState::NEW:
			{
				return IceServer::IceState::NEW;
			}

			case FBS::WebRtcTransport::IceState::CONNECTED:
			{
				return IceServer::IceState::CONNECTED;
			}

			case FBS::WebRtcTransport::IceState::COMPLETED:
			{
				return IceServer::IceState::COMPLETED;
			}

			case FBS::WebRtcTransport::IceState::DISCONNECTED:
			{
				return IceServer::IceState::DISCONNECTED;
			}
		}
	}

	FBS::WebRtcTransport::IceState IceServer::IceStateToFbs(IceServer::IceState state)
	{
		switch (state)
		{
			case IceServer::IceState::NEW:
			{
				return FBS::WebRtcTransport::IceState::NEW;
			}

			case IceServer::IceState::CONNECTED:
			{
				return FBS::WebRtcTransport::IceState::CONNECTED;
			}

			case IceServer::IceState::COMPLETED:
			{
				return FBS::WebRtcTransport::IceState::COMPLETED;
			}

			case IceServer::IceState::DISCONNECTED:
			{
				return FBS::WebRtcTransport::IceState::DISCONNECTED;
			}
		}
	}

	/* Instance methods. */

	IceServer::IceServer(
	  Listener* listener,
	  const std::string& usernameFragment,
	  const std::string& password,
	  uint8_t consentTimeoutSec)
	  : listener(listener), usernameFragment(usernameFragment), password(password)
	{
		MS_TRACE();

		if (consentTimeoutSec == 0u)
		{
			// 0 means disabled so it's a valid value.
		}
		else if (consentTimeoutSec < ConsentCheckMinTimeoutSec)
		{
			MS_WARN_TAG(
			  ice,
			  "consentTimeoutSec cannot be lower than %" PRIu8 " seconds, fixing it",
			  ConsentCheckMinTimeoutSec);

			consentTimeoutSec = ConsentCheckMinTimeoutSec;
		}
		else if (consentTimeoutSec > ConsentCheckMaxTimeoutSec)
		{
			MS_WARN_TAG(
			  ice,
			  "consentTimeoutSec cannot be higher than %" PRIu8 " seconds, fixing it",
			  ConsentCheckMaxTimeoutSec);

			consentTimeoutSec = ConsentCheckMaxTimeoutSec;
		}

		this->consentTimeoutMs = consentTimeoutSec * 1000;

		// Notify the listener.
		this->listener->OnIceServerLocalUsernameFragmentAdded(this, usernameFragment);
	}

	IceServer::~IceServer()
	{
		MS_TRACE();

		// Here we must notify the listener about the removal of current
		// usernameFragments (and also the old one if any) and all sessions.

		this->listener->OnIceServerLocalUsernameFragmentRemoved(this, usernameFragment);

		if (!this->oldUsernameFragment.empty())
		{
			this->listener->OnIceServerLocalUsernameFragmentRemoved(this, this->oldUsernameFragment);
		}

		// Clear all sessions.
		this->isRemovingSessions = true;

		for (const auto storedSession : this->sessions)
		{
			// Notify the listener.
			this->listener->OnIceServerSessionRemoved(this, storedSession);
		}

		this->isRemovingSessions = false;

		// Clear all sessions.
		// NOTE: Do it after notifying the listener since the listener may need to
		// use/read the session being removed so we cannot free it yet.
		this->sessions.clear();

		// Unset selected session.
		this->selectedSession = nullptr;

		// Delete the ICE consent check timer.
		delete this->consentCheckTimer;
		this->consentCheckTimer = nullptr;
	}

	void IceServer::ProcessStunPacket(RTC::StunPacket* packet, Session *session)
	{
		MS_TRACE();

		switch (packet->GetClass())
		{
			case RTC::StunPacket::Class::REQUEST:
			{
				if (this->listener->OnIceServerCheckClosed(this)) {
					QRPC_LOGJ(debug, {
						{"ev","parent connection already closed: STUN Binding Request => 401"},
						{"username",packet->GetUsername()}
					});
					RTC::StunPacket* response = packet->CreateErrorResponse(401);

					response->Serialize(StunSerializeBuffer);
					this->listener->OnIceServerSendStunPacket(this, response, session);

					delete response;
					return;
				}				
				ProcessStunRequest(packet, session);

				break;
			}

			case RTC::StunPacket::Class::INDICATION:
			{
				ProcessStunIndication(packet);

				break;
			}

			case RTC::StunPacket::Class::SUCCESS_RESPONSE:
			case RTC::StunPacket::Class::ERROR_RESPONSE:
			{
				ProcessStunResponse(packet, session);

				break;
			}

			default:
			{
				MS_WARN_TAG(
				  ice, "unknown STUN class %" PRIu16 ", discarded", static_cast<uint16_t>(packet->GetClass()));
			}
		}
	}

	void IceServer::RestartIce(const std::string& usernameFragment, const std::string& password)
	{
		MS_TRACE();

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

		// Restart ICE consent check (if running) to give some time to the
		// client to establish ICE again.
		if (IsConsentCheckSupported() && IsConsentCheckRunning())
		{
			RestartConsentCheck();
		}
	}

	bool IceServer::IsValidSession(const Session *session) const
	{
		MS_TRACE();

		return HasSession(session) != nullptr;
	}

	void IceServer::RemoveSession(Session *session)
	{
		MS_TRACE();

		// If IceServer is removing a session or all sessions (for instance in the
		// destructor), the OnIceServerSessionRemoved() callback may end triggering
		// new calls to RemoveSession(). We must ignore it to avoid double-free issues.
		if (this->isRemovingSessions)
		{
			return;
		}

		Session *removedSession{ nullptr };

		// Find the removed session.
		auto it = this->sessions.begin();

		for (; it != this->sessions.end(); ++it)
		{
			Session *storedSession = *it;
			if (storedSession == session)
			{
				removedSession = storedSession;
				break;
			}
		}

		// If not found, ignore.
		if (!removedSession)
		{
			return;
		}

		// Notify the listener.
		this->isRemovingSessions = true;
		this->listener->OnIceServerSessionRemoved(this, removedSession);
		this->isRemovingSessions = false;

		// Remove it from the list of sessions.
		// NOTE: Do it after notifying the listener since the listener may need to
		// use/read the session being removed so we cannot free it yet.
		this->sessions.erase(it);

		// If this is the selected session, do things.
		if (removedSession == this->selectedSession)
		{
			this->selectedSession = nullptr;

			// Mark the first session as selected session (if any) but only if state was
			// 'connected' or 'completed'.
			if (
			  (this->state == IceState::CONNECTED || this->state == IceState::COMPLETED) &&
			  this->sessions.begin() != this->sessions.end())
			{
				SetSelectedSession(*this->sessions.begin());

				// Restart ICE consent check to let the client send new consent requests
				// on the new selected session.
				if (IsConsentCheckSupported())
				{
					RestartConsentCheck();
				}
			}
			// Or just emit 'disconnected'.
			else
			{
				// Update state.
				this->state = IceState::DISCONNECTED;

				// Reset remote nomination.
				this->remoteNomination = 0u;

				// Notify the listener.
				this->listener->OnIceServerDisconnected(this);

				if (IsConsentCheckSupported() && IsConsentCheckRunning())
				{
					StopConsentCheck();
				}
			}
		}
	}

	void IceServer::ProcessStunRequest(RTC::StunPacket* request, Session *session)
	{
		MS_TRACE();

		MS_DEBUG_DEV("processing STUN request");

		// Must be a Binding method.
		if (request->GetMethod() != RTC::StunPacket::Method::BINDING)
		{
			MS_WARN_TAG(
			  ice,
			  "STUN request with unknown method %#.3x => 400",
			  static_cast<unsigned int>(request->GetMethod()));

			// Reply 400.
			RTC::StunPacket* response = request->CreateErrorResponse(400);

			response->Serialize(StunSerializeBuffer);
			this->listener->OnIceServerSendStunPacket(this, response, session);

			delete response;

			return;
		}

		// Must have FINGERPRINT attribute.
		if (!request->HasFingerprint())
		{
			MS_WARN_TAG(ice, "STUN Binding request without FINGERPRINT attribute => 400");

			// Reply 400.
			RTC::StunPacket* response = request->CreateErrorResponse(400);

			response->Serialize(StunSerializeBuffer);
			this->listener->OnIceServerSendStunPacket(this, response, session);

			delete response;

			return;
		}

		// PRIORITY attribute is required.
		if (request->GetPriority() == 0u)
		{
			MS_WARN_TAG(ice, "STUN Binding request without PRIORITY attribute => 400");

			// Reply 400.
			RTC::StunPacket* response = request->CreateErrorResponse(400);

			response->Serialize(StunSerializeBuffer);
			this->listener->OnIceServerSendStunPacket(this, response, session);

			delete response;

			return;
		}

		// Check authentication.
		switch (request->CheckAuthentication(this->usernameFragment, this->password))
		{
			case RTC::StunPacket::Authentication::OK:
			{
				if (!this->oldUsernameFragment.empty() && !this->oldPassword.empty())
				{
					MS_DEBUG_TAG(ice, "new ICE credentials applied");

					// Notify the listener.
					this->listener->OnIceServerLocalUsernameFragmentRemoved(this, this->oldUsernameFragment);

					this->oldUsernameFragment.clear();
					this->oldPassword.clear();
				}

				break;
			}

			case RTC::StunPacket::Authentication::UNAUTHORIZED:
			{
				// We may have changed our usernameFragment and password, so check the
				// old ones.
				// clang-format off
				if (
				  !this->oldUsernameFragment.empty() &&
				  !this->oldPassword.empty() &&
				  request->CheckAuthentication(
				    this->oldUsernameFragment, this->oldPassword
				  ) == RTC::StunPacket::Authentication::OK
				)
				// clang-format on
				{
					MS_DEBUG_TAG(ice, "using old ICE credentials");

					break;
				}

				MS_WARN_TAG(ice, "wrong authentication in STUN Binding request => 401");

				// Reply 401.
				RTC::StunPacket* response = request->CreateErrorResponse(401);

				response->Serialize(StunSerializeBuffer);
				this->listener->OnIceServerSendStunPacket(this, response, session);

				delete response;

				return;
			}

			case RTC::StunPacket::Authentication::BAD_MESSAGE:
			{
				MS_WARN_TAG(ice, "cannot check authentication in STUN Binding request => 400");

				// Reply 400.
				RTC::StunPacket* response = request->CreateErrorResponse(400);

				response->Serialize(StunSerializeBuffer);
				this->listener->OnIceServerSendStunPacket(this, response, session);

				delete response;

				return;
			}
		}

		// The remote peer must be ICE controlling.
		if (request->GetIceControlled())
		{
			MS_WARN_TAG(ice, "peer indicates ICE-CONTROLLED in STUN Binding request => 487");

			// Reply 487 (Role Conflict).
			RTC::StunPacket* response = request->CreateErrorResponse(487);

			response->Serialize(StunSerializeBuffer);
			this->listener->OnIceServerSendStunPacket(this, response, session);

			delete response;

			return;
		}

		MS_DEBUG_DEV(
		  "valid STUN Binding request [priority:%" PRIu32 ", useCandidate:%s]",
		  static_cast<uint32_t>(request->GetPriority()),
		  request->HasUseCandidate() ? "true" : "false");

		// Create a success response.
		RTC::StunPacket* response = request->CreateSuccessResponse();

		// Add XOR-MAPPED-ADDRESS.
		response->SetXorMappedAddress(session->addr().sa());

		// Authenticate the response.
		if (this->oldPassword.empty())
		{
			response->SetPassword(this->password);
		}
		else
		{
			response->SetPassword(this->oldPassword);
		}

		// Send back.
		response->Serialize(StunSerializeBuffer);
		this->listener->OnIceServerSendStunPacket(this, response, session);

		delete response;

		uint32_t nomination{ 0u };

		if (request->HasNomination())
		{
			nomination = request->GetNomination();
		}

		// Handle the session.
		HandleSession(session, request->HasUseCandidate(), request->HasNomination(), nomination);

		// If state is 'connected' or 'completed' after handling the session, then
		// start or restart ICE consent check (if supported).
		if (IsConsentCheckSupported() && (this->state == IceState::CONNECTED || this->state == IceState::COMPLETED))
		{
			if (IsConsentCheckRunning())
			{
				RestartConsentCheck();
			}
			else
			{
				StartConsentCheck();
			}
		}
	}

	void IceServer::ProcessStunIndication(RTC::StunPacket* indication)
	{
		MS_TRACE();

		MS_DEBUG_DEV("STUN indication received, discarded");

		// Nothig else to do. We just discard STUN indications.
	}

	void IceServer::ProcessStunResponse(RTC::StunPacket* response, Session *session)
	{
		MS_TRACE();

		const std::string responseType = response->GetClass() == RTC::StunPacket::Class::SUCCESS_RESPONSE
		                                   ? "success"
		                                   : std::to_string(response->GetErrorCode()) + " error";

		MS_DEBUG_DEV("processing STUN %s response received, discarded", responseType.c_str());

		// Nothig else to do. We just discard STUN responses because we do not
		// generate STUN requests.
		switch (response->GetClass())
		{
		case RTC::StunPacket::Class::SUCCESS_RESPONSE:
			{
				// QRPC_LOG(debug, "STUN Binding Success Response processed");
				this->listener->OnIceServerSuccessResponded(this, response, session);
				break;
			}

			case RTC::StunPacket::Class::ERROR_RESPONSE:
			{
				QRPC_LOGJ(debug, {{"ev","STUN Binding Error Response processed"},{"username",response->GetUsername()}});
				this->listener->OnIceServerErrorResponded(this, response, session);
				break;
			}
		}
	}

	void IceServer::MayForceSelectedSession(const Session *session)
	{
		MS_TRACE();

		if (this->state != IceState::CONNECTED && this->state != IceState::COMPLETED)
		{
			MS_WARN_TAG(ice, "cannot force selected session if not in state 'connected' or 'completed'");

			return;
		}

		auto* storedSession = HasSession(session);

		if (!storedSession)
		{
			MS_WARN_TAG(ice, "cannot force selected session if the given session was not already a valid one");

			return;
		}

		SetSelectedSession(storedSession);
	}

	void IceServer::HandleSession(
	  Session *session, bool hasUseCandidate, bool hasNomination, uint32_t nomination)
	{
		MS_TRACE();

		switch (this->state)
		{
			case IceState::NEW:
			{
				// There shouldn't be a selected session.
				MS_ASSERT(!this->selectedSession, "state is 'new' but there is selected session");

				if (!hasUseCandidate && !hasNomination)
				{
					MS_DEBUG_TAG(
					  ice,
					  "transition from state 'new' to 'connected' [hasUseCandidate:%s, hasNomination:%s, nomination:%" PRIu32
					  "]",
					  hasUseCandidate ? "true" : "false",
					  hasNomination ? "true" : "false",
					  nomination);

					// Store the session.
					auto* storedSession = AddSession(session);

					// Update state.
					this->state = IceState::CONNECTED;

					// Mark it as selected session.
					SetSelectedSession(storedSession);

					// Notify the listener.
					this->listener->OnIceServerConnected(this);
				}
				else
				{
					// Store the session.
					auto* storedSession = AddSession(session);

					if ((hasNomination && nomination > this->remoteNomination) || !hasNomination)
					{
						MS_DEBUG_TAG(
						  ice,
						  "transition from state 'new' to 'completed' [hasUseCandidate:%s, hasNomination:%s, nomination:%" PRIu32
						  "]",
						  hasUseCandidate ? "true" : "false",
						  hasNomination ? "true" : "false",
						  nomination);

						// Update state.
						this->state = IceState::COMPLETED;

						// Mark it as selected session.
						SetSelectedSession(storedSession);

						// Update nomination.
						if (hasNomination && nomination > this->remoteNomination)
						{
							this->remoteNomination = nomination;
						}

						// Notify the listener.
						this->listener->OnIceServerCompleted(this);
					}
				}

				break;
			}

			case IceState::DISCONNECTED:
			{
				// There shouldn't be a selected session.
				MS_ASSERT(!this->selectedSession, "state is 'disconnected' but there is selected session");

				if (!hasUseCandidate && !hasNomination)
				{
					MS_DEBUG_TAG(
					  ice,
					  "transition from state 'disconnected' to 'connected' [hasUseCandidate:%s, hasNomination:%s, nomination:%" PRIu32
					  "]",
					  hasUseCandidate ? "true" : "false",
					  hasNomination ? "true" : "false",
					  nomination);

					// Store the session.
					auto* storedSession = AddSession(session);

					// Update state.
					this->state = IceState::CONNECTED;

					// Mark it as selected session.
					SetSelectedSession(storedSession);

					// Notify the listener.
					this->listener->OnIceServerConnected(this);
				}
				else
				{
					// Store the session.
					auto* storedSession = AddSession(session);

					if ((hasNomination && nomination > this->remoteNomination) || !hasNomination)
					{
						MS_DEBUG_TAG(
						  ice,
						  "transition from state 'disconnected' to 'completed' [hasUseCandidate:%s, hasNomination:%s, nomination:%" PRIu32
						  "]",
						  hasUseCandidate ? "true" : "false",
						  hasNomination ? "true" : "false",
						  nomination);

						// Update state.
						this->state = IceState::COMPLETED;

						// Mark it as selected session.
						SetSelectedSession(storedSession);

						// Update nomination.
						if (hasNomination && nomination > this->remoteNomination)
						{
							this->remoteNomination = nomination;
						}

						// Notify the listener.
						this->listener->OnIceServerCompleted(this);
					}
				}

				break;
			}

			case IceState::CONNECTED:
			{
				// There should be some sessions.
				MS_ASSERT(!this->sessions.empty(), "state is 'connected' but there are no sessions");

				// There should be a selected session.
				MS_ASSERT(this->selectedSession, "state is 'connected' but there is not selected session");

				if (!hasUseCandidate && !hasNomination)
				{
					// Store the session.
					AddSession(session);
				}
				else
				{
					MS_DEBUG_TAG(
					  ice,
					  "transition from state 'connected' to 'completed' [hasUseCandidate:%s, hasNomination:%s, nomination:%" PRIu32
					  "]",
					  hasUseCandidate ? "true" : "false",
					  hasNomination ? "true" : "false",
					  nomination);

					// Store the session.
					auto* storedSession = AddSession(session);

					if ((hasNomination && nomination > this->remoteNomination) || !hasNomination)
					{
						// Update state.
						this->state = IceState::COMPLETED;

						// Mark it as selected session.
						SetSelectedSession(storedSession);

						// Update nomination.
						if (hasNomination && nomination > this->remoteNomination)
						{
							this->remoteNomination = nomination;
						}

						// Notify the listener.
						this->listener->OnIceServerCompleted(this);
					}
				}

				break;
			}

			case IceState::COMPLETED:
			{
				// There should be some sessions.
				MS_ASSERT(!this->sessions.empty(), "state is 'completed' but there are no sessions");

				// There should be a selected session.
				MS_ASSERT(this->selectedSession, "state is 'completed' but there is not selected session");

				if (!hasUseCandidate && !hasNomination)
				{
					// Store the session.
					AddSession(session);
				}
				else
				{
					// Store the session.
					auto* storedSession = AddSession(session);

					if ((hasNomination && nomination > this->remoteNomination) || !hasNomination)
					{
						// Mark it as selected session.
						SetSelectedSession(storedSession);

						// Update nomination.
						if (hasNomination && nomination > this->remoteNomination)
						{
							this->remoteNomination = nomination;
						}
					}
				}

				break;
			}
		}
	}

	Session *IceServer::AddSession(Session *session)
	{
		MS_TRACE();

		auto* storedSession = HasSession(session);

		if (storedSession)
		{
			MS_DEBUG_DEV("session already exists");

			return storedSession;
		}

		// Add the new session at the beginning of the list.
		this->sessions.push_front(session);

		storedSession = *this->sessions.begin();

    // base::Session already stored its remote address on creation
		// // If it is UDP then we must store the remote address (until now it is
		// // just a pointer that will be freed soon).
		// if (storedTuple->GetProtocol() == TransportTuple::Protocol::UDP)
		// {
		// 	storedTuple->StoreUdpRemoteAddress();
		// }

		// Notify the listener.
		this->listener->OnIceServerSessionAdded(this, storedSession);

		// Don't allow more than MaxSessions.
		if (this->sessions.size() > MaxSessions)
		{
			MS_WARN_TAG(ice, "too too many sessions, removing the oldest non selected one");

			// Find the oldest session which is neither the added one nor the selected
			// one (if any), and remove it.
			Session *removedSession{ nullptr };
			auto it = this->sessions.rbegin();

			for (; it != this->sessions.rend(); ++it)
			{
				Session *otherStoredSession = *it;

				if (otherStoredSession != storedSession && otherStoredSession != this->selectedSession)
				{
					removedSession = otherStoredSession;

					break;
				}
			}

			// This should not happen by design.
			MS_ASSERT(removedSession, "couldn't find any session to be removed");

			// Notify the listener.
			this->isRemovingSessions = true;
			this->listener->OnIceServerSessionRemoved(this, removedSession);
			this->isRemovingSessions = false;

			// Remove it from the list of sessions.
			// NOTE: Do it after notifying the listener since the listener may need to
			// use/read the session being removed so we cannot free it yet.
			// NOTE: This trick is needed since it is a reverse_iterator and
			// erase() requires a iterator, const_iterator or bidirectional_iterator.
			this->sessions.erase(std::next(it).base());
		}

		// Return the address of the inserted session.
		return storedSession;
	}

	inline Session *IceServer::HasSession(const Session *session) const
	{
		MS_TRACE();

		// Check the current selected session (if any).
		if (this->selectedSession && this->selectedSession == session)
		{
			return this->selectedSession;
		}

		// Otherwise check other stored sessions.
		for (const auto& it : this->sessions)
		{
			Session *storedSession = it;

			if (storedSession == session)
			{
				return storedSession;
			}
		}

		return nullptr;
	}

	inline void IceServer::SetSelectedSession(Session* storedSession)
	{
		MS_TRACE();

		// If already the selected session do nothing.
		if (storedSession == this->selectedSession)
		{
			return;
		}

		this->selectedSession = storedSession;

		// Notify the listener.
		this->listener->OnIceServerSelectedSession(this, this->selectedSession);
	}

	void IceServer::StartConsentCheck()
	{
		MS_TRACE();

		MS_ASSERT(IsConsentCheckSupported(), "ICE consent check not supported");
		MS_ASSERT(!IsConsentCheckRunning(), "ICE consent check already running");
		MS_ASSERT(this->selectedSession, "no selected session");

		// Create the ICE consent check timer if it doesn't exist.
		if (!this->consentCheckTimer)
		{
			this->consentCheckTimer = new TimerHandle(this);
		}

		this->consentCheckTimer->Start(this->consentTimeoutMs);
	}

	void IceServer::RestartConsentCheck()
	{
		MS_TRACE();

		MS_ASSERT(IsConsentCheckSupported(), "ICE consent check not supported");
		MS_ASSERT(IsConsentCheckRunning(), "ICE consent check not running");
		MS_ASSERT(this->selectedSession, "no selected session");

		this->consentCheckTimer->Restart();
	}

	void IceServer::StopConsentCheck()
	{
		MS_TRACE();

		MS_ASSERT(IsConsentCheckSupported(), "ICE consent check not supported");
		MS_ASSERT(IsConsentCheckRunning(), "ICE consent check not running");

		this->consentCheckTimer->Stop();
	}

	inline void IceServer::OnTimer(TimerHandle* timer)
	{
		MS_TRACE();

		if (timer == this->consentCheckTimer)
		{
			MS_ASSERT(IsConsentCheckSupported(), "ICE consent check not supported");

			// State must be 'connected' or 'completed'.
			MS_ASSERT(
			  this->state == IceState::COMPLETED || this->state == IceState::CONNECTED,
			  "ICE consent check timer fired but state is neither 'completed' nor 'connected'");

			// There should be a selected session.
			MS_ASSERT(this->selectedSession, "ICE consent check timer fired but there is not selected session");

			MS_WARN_TAG(ice, "ICE consent expired due to timeout, moving to 'disconnected' state");

			// Update state.
			this->state = IceState::DISCONNECTED;

			// Reset remote nomination.
			this->remoteNomination = 0u;

			// Clear all sessions.
			this->isRemovingSessions = true;

			for (const auto storedSession : this->sessions)
			{
				// Notify the listener.
				this->listener->OnIceServerSessionRemoved(this, storedSession);
			}

			this->isRemovingSessions = false;

			// Clear all sessions.
			// NOTE: Do it after notifying the listener since the listener may need to
			// use/read the session being removed so we cannot free it yet.
			this->sessions.clear();

			// Unset selected session.
			this->selectedSession = nullptr;

			// Notify the listener.
			this->listener->OnIceServerDisconnected(this);
		}
	}

	// IceProber
	void IceProber::Success() {
		last_success_ = qrpc_time_now();
		state_ = CONNECTED;
	}
	void IceProber::SendBindingRequest(Session *s) {
		static thread_local TxId tx_id;
		static thread_local RTC::StunPacket *stun_packet = new RTC::StunPacket(
			RTC::StunPacket::Class::REQUEST, RTC::StunPacket::Method::BINDING, tx_id, nullptr, 0);
		uint8_t stun_buffer[1024];
		random::bytes(tx_id, sizeof(TxId));
		ASSERT(!ufrag_.empty() && !pwd_.empty());
		stun_packet->SetUsername((ufrag_ + ":").c_str(), ufrag_.length() + 1);
		stun_packet->SetPassword(pwd_);
		// https://speakerdeck.com/iwashi86/webrtc-ice-internals?slide=60
		stun_packet->SetPriority(0x7e0000);
		stun_packet->SetIceControlling(1);
		stun_packet->Serialize(stun_buffer);
		s->Send(
			reinterpret_cast<const char *>(stun_buffer), stun_packet->GetSize()
		);
	}
  qrpc_time_t IceProber::OnTimer(Session *s) {
		// this interval setting is based on observing chrome's behaviour
		// TODO: is there standard interval for this?
		auto now = qrpc_time_now();
    SendBindingRequest(s);
    switch (state_) {
    case NEW:
			state_ = DISCONNECTED;
			last_success_ = now;
      return now + qrpc_time_msec(50);
    case CONNECTED:
      if (now - last_success_ > qrpc_time_msec(2500)) {
        state_ = CHECKING;
        return now + qrpc_time_sec(1);
      }
      return now + qrpc_time_msec(2500);
    case CHECKING:
      if (now - last_success_ > disconnect_timeout_) {
        state_ = DISCONNECTED;
        return now + qrpc_time_msec(50);
      }
      return now + qrpc_time_sec(1);
    case DISCONNECTED:
      if (now - last_success_ > failed_timeout_) {
        state_ = FAILED;
      }
      return now + qrpc_time_msec(50);
    case FAILED:
		case STOPPED:
      state_ = NEW;
			last_success_ = 0;
      return 0ULL; // alarm stops
    default:
      ASSERT(false);
      return 0ULL;
    }
  }
} // namespace webrtc
} // namespace base
