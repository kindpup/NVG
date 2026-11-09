//------------------------------------------------------------------------------------------------
// ACM_PlayerLinkComponent.c
// The owner-bound pipe between one player and the server. Lives on the character prefab.
//
// WHY THIS COMPONENT EXISTS AT ALL
//
// Everything else in the tactical system could have hung off the game mode. This could not, and the
// reason is identity. A game-mode component is addressed by every client as the same instance, so
// Enfusion has no way to tell the server which client an RPC came from - the server has to take the
// client's word for who it is. That is fine for nothing and fatal for two things: a report claiming
// to come from someone else's sensor, and a role change applied to someone else's player.
//
// A component on the character is bound to one connection. The server reads the player id off THIS
// component's own owner entity, so identity is derived rather than claimed, and the argument that
// used to carry it simply does not exist any more.
//
// It also solves scoping. Contacts are faction-restricted, and RplRcver.Owner lets the server
// deliver down one player's pipe instead of broadcasting and trusting clients to ignore what is not
// theirs. There is no filtering step to get wrong, because the wrong data never leaves the server.
//
// TWO PAYLOADS, ONE PIPE. Contacts and roles both travel here because both need the same thing -
// a server that knows who is asking. Building that path twice would have been the wasteful option.
//------------------------------------------------------------------------------------------------

[ComponentEditorProps(category: "ACM/Tactical", description: "Owner-bound tactical link between a player and the server. Add to the character prefab.")]
class ACM_PlayerLinkComponentClass : ScriptComponentClass {}

class ACM_PlayerLinkComponent : ScriptComponent
{
	// Traces every step a report takes - each one can drop it silently. Off in production.
	protected static const bool ACM_LINK_DEBUG = false;

	// The local player's own link, cached on the client for the reporting path.
	protected static ACM_PlayerLinkComponent s_LocalInstance;

	protected int m_iPlayerId = -1;
	protected bool m_bIsLocal;

	// Client-side throttle, so a client mostly rate-limits itself and the server's budget is a
	// backstop against a modified one rather than the primary mechanism.
	protected ref map<int, float> m_mNextReportTime = new map<int, float>();

	// contactId the local client has been told corresponds to a target it personally reported.
	// Populated ONLY by acknowledgement of the client's own reports, so it can never contain a
	// binding for a target this client has not seen.
	protected ref map<int, int> m_mLocalRplToContact = new map<int, int>();

	protected bool m_bSyncRequested;

	// Fail closed until the authoritative snapshot arrives, including join-in-progress/respawn.
	[RplProp()]
	protected int m_iServerIffMask;

	int GetServerIffMask() { return m_iServerIffMask; }


	//------------------------------------------------------------------------------------------------
	static ACM_PlayerLinkComponent GetLocal()
	{
		return s_LocalInstance;
	}

	//------------------------------------------------------------------------------------------------
	// Server-side lookup of a given player's pipe, used by the contact manager to deliver.
	//------------------------------------------------------------------------------------------------
	static ACM_PlayerLinkComponent GetForPlayer(int playerId)
	{
		PlayerManager playerManager = GetGame().GetPlayerManager();
		if (!playerManager)
			return null;

		IEntity controlled = playerManager.GetPlayerControlledEntity(playerId);
		if (!controlled)
			return null;

		return ACM_PlayerLinkComponent.Cast(controlled.FindComponent(ACM_PlayerLinkComponent));
	}

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		SetEventMask(owner, EntityEvent.INIT);
	}

	//------------------------------------------------------------------------------------------------
	override void EOnInit(IEntity owner)
	{
		if (!SCR_Global.IsEditMode() && (!Replication.IsRunning() || Replication.IsServer()))
		{
			m_iServerIffMask = ACM_IffServerSettings.ServerMask();
			Replication.BumpMe();
		}

		// Deferred: the player controller is not necessarily bound to the character at component
		// init, so resolving identity here would resolve it to nothing.
		GetGame().GetCallqueue().CallLater(ResolveIdentity, 500, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void ResolveIdentity()
	{
		IEntity owner = GetOwner();
		if (!owner)
			return;

		PlayerManager playerManager = GetGame().GetPlayerManager();
		if (playerManager)
			m_iPlayerId = playerManager.GetPlayerIdFromControlledEntity(owner);

		if (m_iPlayerId <= 0)
		{
			// AI characters carry this component too and simply never resolve a player. Retry a few
			// times for a human whose controller has not attached yet, then stop.
			GetGame().GetCallqueue().CallLater(ResolveIdentity, 1000, false);
			return;
		}

		m_bIsLocal = (SCR_PlayerController.GetLocalControlledEntity() == owner);

		if (m_bIsLocal)
			s_LocalInstance = this;

		if (ACM_LINK_DEBUG)
			Print(string.Format("[ACM Link] identity resolved playerId=%1 isLocal=%2 server=%3 manager=%4",
				m_iPlayerId, m_bIsLocal,
				Replication.IsRunning() && Replication.IsServer(),
				ACM_TacticalContactManager.GetInstance() != null), LogLevel.DEBUG);

		RequestInitialSync();
	}

	//------------------------------------------------------------------------------------------------
	// REPORTING - client side
	//
	// Called by the detection system whenever the local scan confirms a hostile by raycast. Every
	// gate here is a courtesy to the server rather than a security measure: a modified client can
	// skip all of it, which is exactly why the server repeats the ones that matter.
	//------------------------------------------------------------------------------------------------
	void OfferSighting(ACM_TDContact contact, float now)
	{
		if (!m_bIsLocal || !contact || !contact.entity)
		{
			if (ACM_LINK_DEBUG && contact)
				TraceOffer(contact, "not local, or no entity");

			return;
		}

		// Hostiles only, and this is a hard gate rather than an optimisation.
		//
		// Nothing else can reach here today - non-combatants are forked out of the pipeline before a
		// contact object exists, so a civilian has no ACM_TDContact to be offered with. This is the
		// belt to that braces: if some future sensor ever files a contact without going through the
		// scan fork, the data link still refuses to carry anything that is not classified hostile.
		//
		// The server re-derives everything about a report it accepts. It does not re-derive
		// classification, so this is the last place the question is asked.
		if (contact.affiliation != ACM_TDAffiliation.HOSTILE)
		{
			if (ACM_LINK_DEBUG)
				TraceOffer(contact, "not classified hostile - never reported");

			return;
		}

		// Compared against INVALID_ID, not against zero.
		//
		// This guard originally read "targetId < 0" and rejected every report the link was ever
		// offered, because an RplId has its high bit set and is always negative read as an int. The
		// symptom was a data link that compiled, initialised, resolved identity correctly and then
		// silently did nothing at all.
		if (!contact.HasNetworkIdentity())
		{
			if (ACM_LINK_DEBUG)
				TraceOffer(contact, "no network identity - target is not a replicated entity");

			return;
		}

		ACM_TargetDetectionComponent provider = ACM_TargetDetectionSystem.GetInstance().GetActiveProvider();
		if (!provider || !provider.SharesContacts())
		{
			if (ACM_LINK_DEBUG)
				TraceOffer(contact, "no provider, or sharing disabled on the device");

			return;
		}

		float nextAllowed;
		if (m_mNextReportTime.Find(contact.targetId, nextAllowed) && now < nextAllowed)
			return;

		m_mNextReportTime.Set(contact.targetId, now + provider.GetReportIntervalSeconds());

		if (!Replication.IsRunning())
		{
			if (ACM_LINK_DEBUG)
				TraceOffer(contact, "replication not running");

			return;
		}

		if (ACM_LINK_DEBUG)
			TraceOffer(contact, "SENDING");

		// No range on the wire. The server reads it off the device the reporter is actually wearing.
		Rpc(Rpc_AskReportContact, contact.targetId, contact.lastKnownPos, ACM_TDSensor.VISUAL);
	}

	//------------------------------------------------------------------------------------------------
	// REPORTING - server side.
	//
	// Note what this signature does NOT contain: a player id. The server derives it from this
	// component's own owner, which is the entire point of routing through an owned entity. There is
	// no argument here a client could forge to report as somebody else.
	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void Rpc_AskReportContact(int targetRplId, vector reportedPos, ACM_TDSensor sensor)
	{
		ACM_TacticalContactManager manager = ACM_TacticalContactManager.GetInstance();
		if (!manager)
		{
			if (ACM_LINK_DEBUG)
				Print("[ACM Link] SERVER received report but there is no contact manager on this game mode", LogLevel.WARNING);

			return;
		}

		IEntity owner = GetOwner();
		if (!owner)
			return;

		PlayerManager playerManager = GetGame().GetPlayerManager();
		if (!playerManager)
			return;

		int reporterId = playerManager.GetPlayerIdFromControlledEntity(owner);
		if (reporterId <= 0)
			return;

		int contactId;
		ACM_TDRejectReason verdict = manager.SubmitReport(reporterId, owner, targetRplId, reportedPos, sensor, contactId);

		if (ACM_LINK_DEBUG)
			Print(string.Format("[ACM Link] SERVER report from player %1 -> %2 (contactId=%3)",
				reporterId, SCR_Enum.GetEnumName(ACM_TDRejectReason, verdict), contactId), LogLevel.DEBUG);

		if (verdict != ACM_TDRejectReason.ACCEPTED)
			return;

		// The reporter - and only the reporter - learns which contactId its target became.
		//
		// Safe by inspection: this client already holds that entity, because it just raycast to it.
		// The binding tells it nothing it could not already do. What the binding buys is that when
		// the shared contact comes back down the link, the client can recognise it as the same enemy
		// it is already looking at and merge the two instead of drawing one target twice.
		//
		// Nobody else receives this, ever. To any other client the contactId is a number with no
		// route back to an entity.
		Rpc(Rpc_AckContactBinding, targetRplId, contactId);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void Rpc_AckContactBinding(int targetRplId, int contactId)
	{
		m_mLocalRplToContact.Set(targetRplId, contactId);

		ACM_TargetDetectionSystem.GetInstance().BindLocalContact(targetRplId, contactId);
	}

	//------------------------------------------------------------------------------------------------
	// DELIVERY - server side. Pushes one contact down this player's pipe.
	//
	// Every field copied out individually rather than handing over the contact object, so the
	// payload cannot silently widen when a field is added to ACM_TDLinkContact. targetRplId is
	// conspicuously absent and must stay that way: Replication.FindItem would turn it back into a
	// live entity handle on the receiving client.
	//------------------------------------------------------------------------------------------------
	void PushContact(ACM_TDLinkContact contact, float now)
	{
		if (!contact || !Replication.IsRunning())
			return;

		float age = 0;
		if (contact.lastConfirmedTime > 0)
			age = now - contact.lastConfirmedTime;

		Rpc(Rpc_DeliverContact,
			contact.contactId,
			contact.lastKnownPos,
			contact.sensor,
			age,
			contact.reporterPlayerId);
	}

	//------------------------------------------------------------------------------------------------
	// DELIVERY - client side.
	//
	// Everything arriving here becomes DATALINK delivery, unconditionally. Not "unless the client
	// also sees it" - a client that also sees the target has its own LOCAL contact already, and the
	// merge happens through the binding rather than by promoting this one.
	//
	// This is the correction that matters most in the whole phase: sensor and delivery are separate
	// axes. A hostile spotted by a soldier and shared to the platoon stays sensor=VISUAL on every
	// machine, because that describes the observation - but delivery is DATALINK on every machine
	// except the spotter's, because that describes who can actually see it. Collapsing the two would
	// tell forty clients they personally have eyes on something none of them can see.
	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void Rpc_DeliverContact(int contactId, vector lastKnownPos, ACM_TDSensor sensor, float age, int reporterPlayerId)
	{
		if (ACM_LINK_DEBUG)
			Print(string.Format("[ACM Link] RECEIVED contact %1 at %2 age=%3s from player %4", contactId, lastKnownPos, age, reporterPlayerId), LogLevel.DEBUG);

		ACM_TargetDetectionSystem system = ACM_TargetDetectionSystem.GetInstance();
		if (!system)
			return;

		system.IngestLinkContact(contactId, lastKnownPos, sensor, age, reporterPlayerId);
	}

	//------------------------------------------------------------------------------------------------
	protected void TraceOffer(ACM_TDContact contact, string outcome)
	{
		Print(string.Format("[ACM Link] offer targetId=%1 isLocal=%2 -> %3", contact.targetId, m_bIsLocal, outcome), LogLevel.DEBUG);
	}

	//------------------------------------------------------------------------------------------------
	// JIP. The client asks once it has an identity; the server replies with the contacts this
	// player's own side is entitled to and nothing else.
	//------------------------------------------------------------------------------------------------
	protected void RequestInitialSync()
	{
		if (!m_bIsLocal || m_bSyncRequested || !Replication.IsRunning())
			return;

		m_bSyncRequested = true;
		Rpc(Rpc_AskInitialSync);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void Rpc_AskInitialSync()
	{
		ACM_TacticalContactManager manager = ACM_TacticalContactManager.GetInstance();
		if (!manager)
			return;

		IEntity owner = GetOwner();
		if (!owner)
			return;

		PlayerManager playerManager = GetGame().GetPlayerManager();
		if (!playerManager)
			return;

		// Derived from the owner entity, as everywhere else on this component - the client does not
		// get to say which player's picture it is asking for.
		int receiverId = playerManager.GetPlayerIdFromControlledEntity(owner);
		if (receiverId <= 0)
			return;

		manager.SyncToReceiver(this, receiverId);
	}

	//------------------------------------------------------------------------------------------------
	// Eligibility deliberately does NOT live here any more.
	//
	// This component used to answer "may I receive this?" with a faction comparison. That was a
	// second opinion on a question the contact manager also had to answer, and two places deciding
	// the same thing is how a scope rule ends up enforced on one path and not the other. The manager
	// owns it now - see CanReceiveContact - and this component is purely a pipe.
	//------------------------------------------------------------------------------------------------

	//------------------------------------------------------------------------------------------------
	// ROLES
	//
	// Same pipe, same reason. The old path took a playerId as an RPC argument on a game-mode
	// component, which meant the server had to believe whatever the client wrote there - so a
	// modified client could set someone else's role. Routing through the character means the server
	// reads the id off this component's owner and the forgeable argument is gone.
	//------------------------------------------------------------------------------------------------
	void RequestRole(ACM_ETacticalRole role)
	{
		if (!m_bIsLocal)
			return;

		if (!Replication.IsRunning())
		{
			ACM_TacticalRoleComponent registry = ACM_TacticalRoleComponent.GetInstance();
			if (registry && m_iPlayerId > 0)
				registry.ApplyRoleAuthoritative(m_iPlayerId, role);

			return;
		}

		Rpc(Rpc_AskSetRole, role);
	}

	//------------------------------------------------------------------------------------------------
	// IVAS VISOR
	//
	// Third payload on this pipe, for the same reason as the other two: the server has to know who is
	// asking. A visor request carries no identity argument - the server reads the player off this
	// component's owner - so a client cannot flip somebody else's visor by lying about an id.
	//
	// The device is resolved from the character rather than sent, for the same reason. The client
	// says "my visor, down"; it does not get to nominate which device that means.
	//------------------------------------------------------------------------------------------------
	//------------------------------------------------------------------------------------------------
	// SQUAD COMMAND - client asks, server decides.
	//
	// Routed through this component, on the asking player's OWN character, for the reason the role
	// RPC already documents: the server identifies the caller from the RPC's routing rather than
	// from a field in the message, so a modified client cannot claim to be another player. None of
	// these carry a "who am I" argument, and none of them can be made to.
	//
	// Every one is validated again on arrival. Nothing here is a permission check - these are just
	// the wire. ACM_SquadCommandComponent re-derives group and leadership from live state and
	// refuses anything it does not like.
	//------------------------------------------------------------------------------------------------
	void AskSetMemberRole(int targetPlayerId, int role)
	{
		if (!m_bIsLocal)
			return;

		if (!Replication.IsRunning())
		{
			ServerSquadSetRole(GetOwnerPlayerId(), targetPlayerId, role);
			return;
		}

		Rpc(Rpc_AskSetMemberRole, targetPlayerId, role);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void Rpc_AskSetMemberRole(int targetPlayerId, int role)
	{
		ServerSquadSetRole(GetOwnerPlayerId(), targetPlayerId, role);
	}

	//------------------------------------------------------------------------------------------------
	void AskSetSquadSize(int size)
	{
		if (!m_bIsLocal)
			return;

		if (!Replication.IsRunning())
		{
			ServerSquadSetSize(GetOwnerPlayerId(), size);
			return;
		}

		Rpc(Rpc_AskSetSquadSize, size);
	}

	//------------------------------------------------------------------------------------------------
	//! Rename the group. Leader-only, enforced on the server - this only asks.
	void AskSetGroupName(string name)
	{
		if (!m_bIsLocal)
			return;

		if (!Replication.IsRunning())
		{
			ServerSquadSetName(GetOwnerPlayerId(), name);
			return;
		}

		Rpc(Rpc_AskSetGroupName, name);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void Rpc_AskSetGroupName(string name)
	{
		ServerSquadSetName(GetOwnerPlayerId(), name);
	}

	//------------------------------------------------------------------------------------------------
	protected void ServerSquadSetName(int callerPlayerId, string name)
	{
		ACM_SquadCommandComponent command = ACM_SquadCommandComponent.GetInstance();
		if (command)
			command.ServerSetGroupName(callerPlayerId, name);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void Rpc_AskSetSquadSize(int size)
	{
		ServerSquadSetSize(GetOwnerPlayerId(), size);
	}

	//------------------------------------------------------------------------------------------------
	//! action: 0 open, 1 close, 2 request, 3 approve, 4 deny. One RPC rather than five, because the
	//! server switch below is where the real validation lives and five near-identical wire calls
	//! would be five places to forget it.
	void AskSquadLink(int action, int otherGroupId)
	{
		if (!m_bIsLocal)
			return;

		if (!Replication.IsRunning())
		{
			ServerSquadLink(GetOwnerPlayerId(), action, otherGroupId);
			return;
		}

		Rpc(Rpc_AskSquadLink, action, otherGroupId);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void Rpc_AskSquadLink(int action, int otherGroupId)
	{
		ServerSquadLink(GetOwnerPlayerId(), action, otherGroupId);
	}

	//------------------------------------------------------------------------------------------------
	// Server-side dispatch. Shared by the RPC path and the no-replication path so single player and
	// the editor take exactly the same route as a dedicated server.
	//------------------------------------------------------------------------------------------------
	protected void ServerSquadSetRole(int callerPlayerId, int targetPlayerId, int role)
	{
		ACM_SquadCommandComponent command = ACM_SquadCommandComponent.GetInstance();
		if (command)
			command.ServerSetMemberRole(callerPlayerId, targetPlayerId, role);
	}

	//------------------------------------------------------------------------------------------------
	protected void ServerSquadSetSize(int callerPlayerId, int size)
	{
		ACM_SquadCommandComponent command = ACM_SquadCommandComponent.GetInstance();
		if (command)
			command.ServerSetSquadSize(callerPlayerId, size);
	}

	//------------------------------------------------------------------------------------------------
	protected void ServerSquadLink(int callerPlayerId, int action, int otherGroupId)
	{
		ACM_SquadCommandComponent command = ACM_SquadCommandComponent.GetInstance();
		if (!command)
			return;

		if (action == 0)
			command.ServerOpenLink(callerPlayerId, otherGroupId);
		else if (action == 1)
			command.ServerCloseLink(callerPlayerId, otherGroupId);
		else if (action == 2)
			command.ServerRequestLink(callerPlayerId, otherGroupId);
		else if (action == 3)
			command.ServerApproveLink(callerPlayerId, otherGroupId);
		else if (action == 4)
			command.ServerDenyLink(callerPlayerId, otherGroupId);
	}

	//------------------------------------------------------------------------------------------------
	//! The player this component's character belongs to, resolved on the SERVER from the entity the
	//! RPC arrived through. Never taken from the message.
	protected int GetOwnerPlayerId()
	{
		PlayerManager players = GetGame().GetPlayerManager();
		if (!players)
			return -1;

		return players.GetPlayerIdFromControlledEntity(GetOwner());
	}

	void AskSetVisor(bool down)
	{
		if (!m_bIsLocal)
			return;

		// Single player and the editor have no replication running, and the local write in
		// ApplyVisorFromNetwork has already done the whole job there.
		if (!Replication.IsRunning())
			return;

		Rpc(Rpc_AskSetVisor, down);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void Rpc_AskSetVisor(bool down)
	{
		// Applied on the server as well as broadcast. The server holds the authoritative copy of the
		// item, so if it never writes the signal itself, a late-joining client streams in a visor
		// sitting at whatever the prefab shipped with.
		ApplyVisorLocally(down);

		Rpc(Rpc_DoSetVisor, down);
	}

	//------------------------------------------------------------------------------------------------
	// NoOwner: the requesting client already applied this the moment the key was pressed, so sending
	// it back would be a second write for no benefit. Everyone else needs it.
	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast, RplCondition.NoOwner)]
	protected void Rpc_DoSetVisor(bool down)
	{
		ApplyVisorLocally(down);
	}

	//------------------------------------------------------------------------------------------------
	// Every machine finds the same device on the same character and writes its own signal - which is
	// exactly how night vision and thermal already behave through the gadget system.
	//------------------------------------------------------------------------------------------------
	protected void ApplyVisorLocally(bool down)
	{
		IEntity owner = GetOwner();
		if (!owner)
			return;

		ACM_TargetDetectionComponent device = ACM_TargetDetectionComponent.GetDeviceForCharacter(owner);
		if (!device)
		{
			if (ACM_LINK_DEBUG)
				Print(string.Format("[ACM Link] visor request for player %1 found no device on the character", m_iPlayerId), LogLevel.DEBUG);

			return;
		}

		device.ApplyVisorFromNetwork(down);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void Rpc_AskSetRole(int role)
	{
		if (!ACM_TacticalRoleCatalog.IsValidRoleValue(role))
			return;

		ACM_TacticalRoleComponent registry = ACM_TacticalRoleComponent.GetInstance();
		if (!registry)
			return;

		IEntity owner = GetOwner();
		if (!owner)
			return;

		PlayerManager playerManager = GetGame().GetPlayerManager();
		if (!playerManager)
			return;

		int playerId = playerManager.GetPlayerIdFromControlledEntity(owner);
		if (playerId <= 0)
			return;

		registry.ApplyRoleAuthoritative(playerId, role);
	}
}
