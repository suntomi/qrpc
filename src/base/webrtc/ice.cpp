#define MS_CLASS "RTC::IceServer"
// #define MS_LOG_DEV_LEVEL 3

#include <utility>

#include "base/logger.h"
#include "base/webrtc/ice.h"

namespace base
{
	/* Static. */

	static constexpr size_t StunSerializeBufferSize{ 65536 };
	thread_local static uint8_t StunSerializeBuffer[StunSerializeBufferSize];
	static constexpr size_t MaxTuples{ 8 };

	/* Instance methods. */

	IceServer::IceServer(Listener* listener, const std::string& usernameFragment, const std::string& password)
	  : listener(listener), usernameFragment(usernameFragment), password(password)
	{
		TRACK();

		// Notify the listener.
		this->listener->OnIceServerLocalUsernameFragmentAdded(this, usernameFragment);
	}

	IceServer::~IceServer()
	{
		TRACK();

		// Here we must notify the listener about the removal of current
		// usernameFragments (and also the old one if any) and all sessions.

		this->listener->OnIceServerLocalUsernameFragmentRemoved(this, usernameFragment);

		if (!this->oldUsernameFragment.empty())
		{
			this->listener->OnIceServerLocalUsernameFragmentRemoved(this, this->oldUsernameFragment);
		}

		for (const auto& it : this->sessions)
		{
			// Notify the listener.
			this->listener->OnIceServerSessionRemoved(this, it);
		}

		this->sessions.clear();
	}

	void IceServer::ProcessStunPacket(RTC::StunPacket* packet, Session *session)
	{
		TRACK();

		// Must be a Binding method.
		if (packet->GetMethod() != RTC::StunPacket::Method::BINDING)
		{
			if (packet->GetClass() == RTC::StunPacket::Class::REQUEST)
			{
				QRPC_LOG(warn, 
				  "ice: unknown method %#.3x in STUN Request => 400",
				  static_cast<unsigned int>(packet->GetMethod())
        );

				// Reply 400.
				RTC::StunPacket* response = packet->CreateErrorResponse(400);

				response->Serialize(StunSerializeBuffer);
				this->listener->OnIceServerSendStunPacket(this, response, session);

				delete response;
			}
			else
			{
				QRPC_LOG(warn, 
				  "ignoring STUN Indication or Response with unknown method %#.3x",
				  static_cast<unsigned int>(packet->GetMethod()));
			}

			return;
		}

		// Must use FINGERPRINT (optional for ICE STUN indications).
		if (!packet->HasFingerprint() && packet->GetClass() != RTC::StunPacket::Class::INDICATION)
		{
			if (packet->GetClass() == RTC::StunPacket::Class::REQUEST)
			{
				QRPC_LOG(warn, "STUN Binding Request without FINGERPRINT => 400");

				// Reply 400.
				RTC::StunPacket* response = packet->CreateErrorResponse(400);

				response->Serialize(StunSerializeBuffer);
				this->listener->OnIceServerSendStunPacket(this, response, session);

				delete response;
			}
			else
			{
				QRPC_LOG(warn, "ignoring STUN Binding Response without FINGERPRINT");
			}

			return;
		}

		switch (packet->GetClass())
		{
			case RTC::StunPacket::Class::REQUEST:
			{
				// USERNAME, MESSAGE-INTEGRITY and PRIORITY are required.
				if (!packet->HasMessageIntegrity() || (packet->GetPriority() == 0u) || packet->GetUsername().empty())
				{
					QRPC_LOG(warn, "mising required attributes in STUN Binding Request => 400");

					// Reply 400.
					RTC::StunPacket* response = packet->CreateErrorResponse(400);

					response->Serialize(StunSerializeBuffer);
					this->listener->OnIceServerSendStunPacket(this, response, session);

					delete response;

					return;
				}

				// Check authentication.
				switch (packet->CheckAuthentication(this->usernameFragment, this->password))
				{
					case RTC::StunPacket::Authentication::OK:
					{
						if (!this->oldUsernameFragment.empty() && !this->oldPassword.empty())
						{
							QRPC_LOG(debug, "new ICE credentials applied");

							// Notify the listener.
							this->listener->OnIceServerLocalUsernameFragmentRemoved(this, this->oldUsernameFragment);

							this->oldUsernameFragment.clear();
							this->oldPassword.clear();
						}

						break;
					}

					case RTC::StunPacket::Authentication::UNAUTHORIZED:
					{
						// We may have changed our usernameFragment and password, so check
						// the old ones.
						// clang-format off
						if (
							!this->oldUsernameFragment.empty() &&
							!this->oldPassword.empty() &&
							packet->CheckAuthentication(this->oldUsernameFragment, this->oldPassword) == RTC::StunPacket::Authentication::OK
						)
						// clang-format on
						{
							QRPC_LOG(debug, "using old ICE credentials");

							break;
						}

						QRPC_LOG(warn, "wrong authentication in STUN Binding Request => 401");

						// Reply 401.
						RTC::StunPacket* response = packet->CreateErrorResponse(401);

						response->Serialize(StunSerializeBuffer);
						this->listener->OnIceServerSendStunPacket(this, response, session);

						delete response;

						return;
					}

					case RTC::StunPacket::Authentication::BAD_REQUEST:
					{
						QRPC_LOG(warn, "cannot check authentication in STUN Binding Request => 400");

						// Reply 400.
						RTC::StunPacket* response = packet->CreateErrorResponse(400);

						response->Serialize(StunSerializeBuffer);
						this->listener->OnIceServerSendStunPacket(this, response, session);

						delete response;

						return;
					}
				}

				// The remote peer must be ICE controlling.
				if (packet->GetIceControlled())
				{
					QRPC_LOG(warn, "peer indicates ICE-CONTROLLED in STUN Binding Request => 487");

					// Reply 487 (Role Conflict).
					RTC::StunPacket* response = packet->CreateErrorResponse(487);

					response->Serialize(StunSerializeBuffer);
					this->listener->OnIceServerSendStunPacket(this, response, session);

					delete response;

					return;
				}

				QRPC_LOG(debug,
				  "processing STUN Binding Request [Priority:%" PRIu32 ", UseCandidate:%s]",
				  static_cast<uint32_t>(packet->GetPriority()),
				  packet->HasUseCandidate() ? "true" : "false");

				// Create a success response.
				RTC::StunPacket* response = packet->CreateSuccessResponse();

				// Add XOR-MAPPED-ADDRESS.
				response->SetXorMappedAddress(session->addr().sa());

				// Authenticate the response.
				if (this->oldPassword.empty())
				{
					response->Authenticate(this->password);
				}
				else
				{
					response->Authenticate(this->oldPassword);
				}

				// Send back.
				response->Serialize(StunSerializeBuffer);
				this->listener->OnIceServerSendStunPacket(this, response, session);

				delete response;

				uint32_t nomination{ 0u };

				if (packet->HasNomination())
				{
					nomination = packet->GetNomination();
				}

				// Handle the session.
				HandleTuple(session, packet->HasUseCandidate(), packet->HasNomination(), nomination);

				break;
			}

			case RTC::StunPacket::Class::INDICATION:
			{
				QRPC_LOG(debug, "STUN Binding Indication processed");

				break;
			}

			case RTC::StunPacket::Class::SUCCESS_RESPONSE:
			{
				QRPC_LOG(debug, "STUN Binding Success Response processed");

				break;
			}

			case RTC::StunPacket::Class::ERROR_RESPONSE:
			{
				QRPC_LOG(debug, "STUN Binding Error Response processed");

				break;
			}
		}
	}

	bool IceServer::IsValidTuple(const Session *session) const
	{
		TRACK();

		return HasTuple(session) != nullptr;
	}

	void IceServer::RemoveTuple(Session *session)
	{
		TRACK();

		Session *removedSession{ nullptr };

		// Find the removed session.
		auto it = this->sessions.begin();

		for (; it != this->sessions.end(); ++it)
		{
			Session *storedSession = *it;

			if (storedSession == session)
			{
				removedSession = session;

				break;
			}
		}

		// If not found, ignore.
		if (!removedSession)
		{
			return;
		}

		// Notify the listener.
		this->listener->OnIceServerSessionRemoved(this, removedSession);

		// Remove it from the list of sessions.
		// NOTE: Do it after notifying the listener since the listener may need to
		// use/read the session being removed so we cannot free it yet.
		this->sessions.erase(it);

		// If this is the selected session, do things.
		if (removedSession == this->selectedSession)
		{
			this->selectedSession = nullptr;

			// Mark the first session as selected session (if any).
			if (this->sessions.begin() != this->sessions.end())
			{
				SetSelectedSession(*this->sessions.begin());
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
			}
		}
	}

	void IceServer::MayForceSelectedSession(const Session *session)
	{
		TRACK();

		if (this->state != IceState::CONNECTED && this->state != IceState::COMPLETED)
		{
			QRPC_LOG(warn, "cannot force selected session if not in state 'connected' or 'completed'");

			return;
		}

		auto* storedTuple = HasTuple(session);

		if (!storedTuple)
		{
			QRPC_LOG(warn, "cannot force selected session if the given session was not already a valid one");

			return;
		}

		// Mark it as selected session.
		SetSelectedSession(storedTuple);
	}

	void IceServer::HandleTuple(
	  Session *session, bool hasUseCandidate, bool hasNomination, uint32_t nomination)
	{
		TRACK();

		switch (this->state)
		{
			case IceState::NEW:
			{
				// There shouldn't be a selected session.
				MASSERT(!this->selectedSession, "state is 'new' but there is selected session");

				if (!hasUseCandidate && !hasNomination)
				{
					QRPC_LOG(
					  debug,
					  "transition from state 'new' to 'connected' [hasUseCandidate:%s, hasNomination:%s, nomination:%" PRIu32
					  "]",
					  hasUseCandidate ? "true" : "false",
					  hasNomination ? "true" : "false",
					  nomination);

					// Store the session.
					auto* storedTuple = AddTuple(session);

					// Mark it as selected session.
					SetSelectedSession(storedTuple);

					// Update state.
					this->state = IceState::CONNECTED;

					// Notify the listener.
					this->listener->OnIceServerConnected(this);
				}
				else
				{
					// Store the session.
					auto* storedTuple = AddTuple(session);

					if ((hasNomination && nomination > this->remoteNomination) || !hasNomination)
					{
						QRPC_LOG(
						  debug,
						  "transition from state 'new' to 'completed' [hasUseCandidate:%s, hasNomination:%s, nomination:%" PRIu32
						  "]",
						  hasUseCandidate ? "true" : "false",
						  hasNomination ? "true" : "false",
						  nomination);

						// Mark it as selected session.
						SetSelectedSession(storedTuple);

						// Update state.
						this->state = IceState::COMPLETED;

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
				MASSERT(!this->selectedSession, "state is 'disconnected' but there is selected session");

				if (!hasUseCandidate && !hasNomination)
				{
					QRPC_LOG(
					  debug,
					  "transition from state 'disconnected' to 'connected' [hasUseCandidate:%s, hasNomination:%s, nomination:%" PRIu32
					  "]",
					  hasUseCandidate ? "true" : "false",
					  hasNomination ? "true" : "false",
					  nomination);

					// Store the session.
					auto* storedTuple = AddTuple(session);

					// Mark it as selected session.
					SetSelectedSession(storedTuple);

					// Update state.
					this->state = IceState::CONNECTED;

					// Notify the listener.
					this->listener->OnIceServerConnected(this);
				}
				else
				{
					// Store the session.
					auto* storedTuple = AddTuple(session);

					if ((hasNomination && nomination > this->remoteNomination) || !hasNomination)
					{
            QRPC_LOG(
              debug,
						  "transition from state 'disconnected' to 'completed' [hasUseCandidate:%s, hasNomination:%s, nomination:%" PRIu32
						  "]",
						  hasUseCandidate ? "true" : "false",
						  hasNomination ? "true" : "false",
						  nomination);

						// Mark it as selected session.
						SetSelectedSession(storedTuple);

						// Update state.
						this->state = IceState::COMPLETED;

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
				MASSERT(!this->sessions.empty(), "state is 'connected' but there are no sessions");

				// There should be a selected session.
				MASSERT(this->selectedSession, "state is 'connected' but there is not selected session");

				if (!hasUseCandidate && !hasNomination)
				{
					// Store the session.
					AddTuple(session);
				}
				else
				{
          QRPC_LOG(
            debug,
					  "transition from state 'connected' to 'completed' [hasUseCandidate:%s, hasNomination:%s, nomination:%" PRIu32
					  "]",
					  hasUseCandidate ? "true" : "false",
					  hasNomination ? "true" : "false",
					  nomination);

					// Store the session.
					auto* storedTuple = AddTuple(session);

					if ((hasNomination && nomination > this->remoteNomination) || !hasNomination)
					{
						// Mark it as selected session.
						SetSelectedSession(storedTuple);

						// Update state.
						this->state = IceState::COMPLETED;

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
				MASSERT(!this->sessions.empty(), "state is 'completed' but there are no sessions");

				// There should be a selected session.
				MASSERT(this->selectedSession, "state is 'completed' but there is not selected session");

				if (!hasUseCandidate && !hasNomination)
				{
					// Store the session.
					AddTuple(session);
				}
				else
				{
					// Store the session.
					auto* storedTuple = AddTuple(session);

					if ((hasNomination && nomination > this->remoteNomination) || !hasNomination)
					{
						// Mark it as selected session.
						SetSelectedSession(storedTuple);

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

	inline Session *IceServer::AddTuple(Session *session)
	{
		TRACK();

		auto* storedSession = HasTuple(session);

		if (storedSession)
		{
			TRACE("session already exists");

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

		// Don't allow more than MaxTuples.
		if (this->sessions.size() > MaxTuples)
		{
			QRPC_LOG(warn, "too too many sessions, removing the oldest non selected one");

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
			MASSERT(removedSession, "couldn't find any session to be removed");

			// Notify the listener.
			this->listener->OnIceServerSessionRemoved(this, removedSession);

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

	inline Session *IceServer::HasTuple(const Session *session) const
	{
		TRACK();

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
		TRACK();

		// If already the selected session do nothing.
		if (storedSession == this->selectedSession)
		{
			return;
		}

		this->selectedSession = storedSession;

		// Notify the listener.
		this->listener->OnIceServerSelectedSession(this, this->selectedSession);
	}
} // namespace RTC
