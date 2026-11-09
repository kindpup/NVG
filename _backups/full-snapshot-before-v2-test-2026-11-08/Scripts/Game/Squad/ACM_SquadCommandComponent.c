//------------------------------------------------------------------------------------------------
// ACM_SquadCommandComponent.c
// Server-authoritative squad command: roster roles, group size, and cross-group data link sharing.
//
// Lives on the GAME MODE, like ACM_TacticalRoleComponent and ACM_TacticalContactManager, because
// it arbitrates between groups and therefore cannot belong to any one of them.
//
//------------------------------------------------------------------------------------------------
// EVERY DECISION IS MADE HERE, ON THE SERVER, FROM LIVE STATE.
//
// Clients ask through ACM_PlayerLinkComponent, which is a component on the asking player's OWN
// character. That is the existing pattern in this mod and it exists for one reason: the server
// learns who is asking from the RPC's own routing rather than from a field in the message, so a
// modified client cannot claim to be somebody else. Every method below re-derives the caller's
// group and leadership from the groups manager AT THE MOMENT OF THE CALL. Nothing trusts a value
// that arrived over the wire beyond the thing being asked for.
//
// The three verbs a leader has - assign a role, resize the group, share the picture - are all
// scoped to their own group. There is no method here that lets a leader touch a group they do not
// lead, and adding one would be the change to refuse in review.
//------------------------------------------------------------------------------------------------
class ACM_SquadCommandComponentClass : ScriptComponentClass {}

class ACM_SquadCommandComponent : ScriptComponent
{
	protected static ACM_SquadCommandComponent s_Instance;

	[Attribute("120", UIWidgets.Slider, desc: "How long a cross-group link request waits for an answer before it is dropped, in seconds. Zero keeps requests until they are answered or the round ends.", category: "ACM Squad Command", params: "0 900 10")]
	protected float m_fRequestLifetime;

	[Attribute("1", UIWidgets.CheckBox, desc: "Allow group leaders to open and request cross-group data links from the squad menu. OFF removes the whole feature at the source - the RPCs still arrive and are refused, so switching it off is not something a client can undo.", category: "ACM Squad Command")]
	protected bool m_bAllowCrossGroupLinks;

	[Attribute("1", UIWidgets.CheckBox, desc: "Allow group leaders to assign tactical roles to other members of their own group. OFF leaves every player setting only their own role, which is the original behaviour.", category: "ACM Squad Command")]
	protected bool m_bAllowLeaderRoleAssign;

	[Attribute("1", UIWidgets.CheckBox, desc: "Allow group leaders to change their group's maximum member count. This drives the VANILLA group system through SCR_AIGroup.SetMaxMembers, so the change is visible everywhere the base game shows group capacity, not just in this menu.", category: "ACM Squad Command")]
	protected bool m_bAllowSquadResize;

	// Pending asks, server-side. Never replicated wholesale - a leader is told only about requests
	// that concern their own group, because the full table is a map of who is talking to whom.
	protected ref array<ref ACM_SquadLinkRequest> m_aRequests = {};

	//------------------------------------------------------------------------------------------------
	static ACM_SquadCommandComponent GetInstance()
	{
		return s_Instance;
	}

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		s_Instance = this;
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		if (s_Instance == this)
			s_Instance = null;

		super.OnDelete(owner);
	}

	//------------------------------------------------------------------------------------------------
	bool AllowsCrossGroupLinks() { return m_bAllowCrossGroupLinks; }
	bool AllowsLeaderRoleAssign() { return m_bAllowLeaderRoleAssign; }
	bool AllowsSquadResize() { return m_bAllowSquadResize; }

	//------------------------------------------------------------------------------------------------
	// LEADERSHIP, RESOLVED FRESH EVERY TIME.
	//
	// Never cached and never taken from the caller. A player who was a leader when they opened the
	// menu and is not one when they click is refused, which is the behaviour you want when leadership
	// changes mid-round.
	//------------------------------------------------------------------------------------------------
	static SCR_AIGroup ResolveGroup(int playerId)
	{
		if (playerId <= 0)
			return null;

		SCR_GroupsManagerComponent groups = SCR_GroupsManagerComponent.GetInstance();
		if (!groups)
			return null;

		return groups.GetPlayerGroup(playerId);
	}

	//------------------------------------------------------------------------------------------------
	static bool IsLeaderOf(int playerId, SCR_AIGroup group)
	{
		if (!group || playerId <= 0)
			return false;

		return group.GetLeaderID() == playerId;
	}

	//------------------------------------------------------------------------------------------------
	//! The asking player's group id, or -1. Used by the menu for display and by every server check.
	static int ResolveGroupId(int playerId)
	{
		SCR_AIGroup group = ResolveGroup(playerId);
		if (!group)
			return -1;

		return group.GetGroupID();
	}

	//------------------------------------------------------------------------------------------------
	// ROLE ASSIGNMENT
	//
	// Leader-only, and only for a player who is in the leader's own group RIGHT NOW. The membership
	// test is the security boundary: without it this becomes "any leader can set any player's role",
	// which is the thing ACM_TacticalRoleComponent's own routing was written to prevent.
	//------------------------------------------------------------------------------------------------
	void ServerSetMemberRole(int callerPlayerId, int targetPlayerId, int role)
	{
		if (Replication.IsRunning() && !Replication.IsServer())
			return;

		if (!m_bAllowLeaderRoleAssign)
			return;

		if (targetPlayerId <= 0)
			return;

		if (!ACM_TacticalRoleCatalog.IsValidRoleValue(role))
			return;

		SCR_AIGroup group = ResolveGroup(callerPlayerId);
		if (!IsLeaderOf(callerPlayerId, group))
			return;

		// Target must be in the caller's group. Compared by group id rather than by object so a
		// stale handle cannot slip through.
		if (ResolveGroupId(targetPlayerId) != group.GetGroupID())
			return;

		ACM_TacticalRoleComponent registry = ACM_TacticalRoleComponent.GetInstance();
		if (!registry)
			return;

		registry.ApplyRoleAuthoritative(targetPlayerId, role);

		Print(string.Format("[ACM Squad] leader %1 set role %2 on player %3 (group %4)", callerPlayerId, role, targetPlayerId, group.GetGroupID()), LogLevel.DEBUG);
	}

	//------------------------------------------------------------------------------------------------
	// GROUP SIZE
	//
	// Goes straight to the VANILLA group. SCR_AIGroup.SetMaxMembers is server-only and the base game
	// replicates it through its own RPC_DoSetMaxMembers, so the new capacity is what every vanilla
	// system sees - the group UI, the spawn logic, the join checks - not a number this mod keeps to
	// one side and has to reconcile.
	//
	// Uncapped by request. The only floor is the group's current population: a leader shrinking the
	// group below the number of people standing in it would leave members in a group that reports
	// itself over capacity, and no vanilla path expects that.
	//------------------------------------------------------------------------------------------------
	void ServerSetSquadSize(int callerPlayerId, int size)
	{
		if (Replication.IsRunning() && !Replication.IsServer())
			return;

		if (!m_bAllowSquadResize)
			return;

		SCR_AIGroup group = ResolveGroup(callerPlayerId);
		if (!IsLeaderOf(callerPlayerId, group))
			return;

		if (size < 1)
			return;

		int occupied = CountMembers(group);
		if (size < occupied)
			size = occupied;

		group.SetMaxMembers(size);

		Print(string.Format("[ACM Squad] leader %1 set group %2 max members to %3", callerPlayerId, group.GetGroupID(), size), LogLevel.DEBUG);
	}

	//------------------------------------------------------------------------------------------------
	// GROUP NAME
	//
	// Writes the VANILLA custom name, the same field the base game's group menu edits, so a rename
	// here shows up as "tesy (Atlas Red 1)" everywhere vanilla prints a group - not as a second name
	// this mod keeps to one side.
	//
	// SetCustomName takes the author's player id as well as the text. The base game records who
	// renamed a group; passing the caller through keeps that record honest rather than attributing
	// every rename to the server.
	//------------------------------------------------------------------------------------------------
	void ServerSetGroupName(int callerPlayerId, string name)
	{
		if (Replication.IsRunning() && !Replication.IsServer())
			return;

		SCR_AIGroup group = ResolveGroup(callerPlayerId);
		if (!IsLeaderOf(callerPlayerId, group))
			return;

		name.TrimInPlace();

		if (name.IsEmpty())
			return;

		if (name.Length() > 32)
			name = name.Substring(0, 32);

		group.SetCustomName(name, callerPlayerId);

		Print(string.Format("[ACM Squad] leader %1 renamed group %2 to '%3'", callerPlayerId, group.GetGroupID(), name), LogLevel.DEBUG);
	}

	//------------------------------------------------------------------------------------------------
	//! Players plus AI agents. Both occupy a slot as far as the vanilla group is concerned.
	static int CountMembers(SCR_AIGroup group)
	{
		if (!group)
			return 0;

		int count = 0;

		array<int> players = group.GetPlayerIDs();
		if (players)
			count += players.Count();

		array<AIAgent> agents = {};
		group.GetAgents(agents);

		PlayerManager manager = GetGame().GetPlayerManager();

		foreach (AIAgent agent : agents)
		{
			if (!agent)
				continue;

			IEntity controlled = agent.GetControlledEntity();
			if (!controlled)
				continue;

			// Skip agents that belong to a player already counted above.
			if (manager && manager.GetPlayerIdFromControlledEntity(controlled) > 0)
				continue;

			count++;
		}

		return count;
	}

	//------------------------------------------------------------------------------------------------
	// OPEN - share this leader's OWN group outward.
	//
	// Needs no consent from anybody, because the leader is giving away data their own group produced.
	// It grants the caller nothing: the direction is strictly own-group -> other-group, and it is
	// never bidirectional, which is what keeps "open" from being a way to help yourself to a feed by
	// opening a link to a squad that then appears to have opened one back.
	//------------------------------------------------------------------------------------------------
	void ServerOpenLink(int callerPlayerId, int toGroupId)
	{
		if (Replication.IsRunning() && !Replication.IsServer())
			return;

		if (!m_bAllowCrossGroupLinks)
			return;

		SCR_AIGroup group = ResolveGroup(callerPlayerId);
		if (!IsLeaderOf(callerPlayerId, group))
			return;

		int fromGroupId = group.GetGroupID();
		if (fromGroupId < 0 || toGroupId < 0 || fromGroupId == toGroupId)
			return;

		ACM_TacticalContactManager manager = ACM_TacticalContactManager.GetInstance();
		if (!manager)
			return;

		// One direction only, deliberately. See the note above.
		manager.ServerGrantLink(fromGroupId, toGroupId, false, 0);

		Print(string.Format("[ACM Squad] leader %1 OPENED group %2 -> %3", callerPlayerId, fromGroupId, toGroupId), LogLevel.DEBUG);
	}

	//------------------------------------------------------------------------------------------------
	//! Stop sharing this leader's own group outward. Symmetric with ServerOpenLink and equally
	//! consent-free, for the same reason: it is their data.
	void ServerCloseLink(int callerPlayerId, int toGroupId)
	{
		if (Replication.IsRunning() && !Replication.IsServer())
			return;

		SCR_AIGroup group = ResolveGroup(callerPlayerId);
		if (!IsLeaderOf(callerPlayerId, group))
			return;

		ACM_TacticalContactManager manager = ACM_TacticalContactManager.GetInstance();
		if (!manager)
			return;

		manager.ServerRevokeLink(group.GetGroupID(), toGroupId);

		Print(string.Format("[ACM Squad] leader %1 CLOSED group %2 -> %3", callerPlayerId, group.GetGroupID(), toGroupId), LogLevel.DEBUG);
	}

	//------------------------------------------------------------------------------------------------
	// REQUEST - ask another group for ITS picture.
	//
	// Files a record and does nothing else. THIS METHOD GRANTS NO ACCESS. It is safe for a client to
	// reach precisely because the thing it produces is inert until the other group's leader acts.
	//------------------------------------------------------------------------------------------------
	void ServerRequestLink(int callerPlayerId, int targetGroupId)
	{
		if (Replication.IsRunning() && !Replication.IsServer())
			return;

		if (!m_bAllowCrossGroupLinks)
			return;

		SCR_AIGroup group = ResolveGroup(callerPlayerId);
		if (!IsLeaderOf(callerPlayerId, group))
			return;

		int fromGroupId = group.GetGroupID();
		if (fromGroupId < 0 || targetGroupId < 0 || fromGroupId == targetGroupId)
			return;

		float now = System.GetTickCount() / 1000.0;
		PruneRequests(now);

		// Re-asking refreshes the existing record rather than stacking duplicates, so a leader
		// spamming the button cannot flood the answering leader's list.
		foreach (ACM_SquadLinkRequest existing : m_aRequests)
		{
			if (existing && existing.Matches(fromGroupId, targetGroupId))
			{
				existing.createdAt = now;
				existing.requesterPlayerId = callerPlayerId;
				return;
			}
		}

		m_aRequests.Insert(new ACM_SquadLinkRequest(fromGroupId, targetGroupId, callerPlayerId, now));

		Print(string.Format("[ACM Squad] leader %1 REQUESTED group %2 <- %3", callerPlayerId, fromGroupId, targetGroupId), LogLevel.DEBUG);
	}

	//------------------------------------------------------------------------------------------------
	// APPROVE - the only path that actually opens a feed.
	//
	// Performed by the leader of the group BEING ASKED, and the grant runs in their favour: the
	// requester receives, the approver shares. A leader can only ever approve a request aimed at
	// their own group, which is what makes this safe to expose.
	//------------------------------------------------------------------------------------------------
	void ServerApproveLink(int callerPlayerId, int requesterGroupId)
	{
		if (Replication.IsRunning() && !Replication.IsServer())
			return;

		if (!m_bAllowCrossGroupLinks)
			return;

		SCR_AIGroup group = ResolveGroup(callerPlayerId);
		if (!IsLeaderOf(callerPlayerId, group))
			return;

		int myGroupId = group.GetGroupID();

		float now = System.GetTickCount() / 1000.0;
		PruneRequests(now);

		int found = -1;
		for (int i = 0; i < m_aRequests.Count(); i++)
		{
			ACM_SquadLinkRequest request = m_aRequests[i];

			// The request must be FOR this leader's group. Approving anything else would be
			// approving on somebody else's behalf.
			if (request && request.Matches(requesterGroupId, myGroupId))
			{
				found = i;
				break;
			}
		}

		if (found < 0)
			return;

		ACM_TacticalContactManager manager = ACM_TacticalContactManager.GetInstance();
		if (!manager)
			return;

		// myGroup shares TO the requester. Not bidirectional: approving somebody's request is not
		// an agreement to receive theirs, and if they want that they can approve their own side.
		manager.ServerGrantLink(myGroupId, requesterGroupId, false, 0);

		m_aRequests.Remove(found);

		Print(string.Format("[ACM Squad] leader %1 APPROVED group %2 -> %3", callerPlayerId, myGroupId, requesterGroupId), LogLevel.DEBUG);
	}

	//------------------------------------------------------------------------------------------------
	//! Refuse a pending request aimed at this leader's group. Removes the record; grants nothing.
	void ServerDenyLink(int callerPlayerId, int requesterGroupId)
	{
		if (Replication.IsRunning() && !Replication.IsServer())
			return;

		SCR_AIGroup group = ResolveGroup(callerPlayerId);
		if (!IsLeaderOf(callerPlayerId, group))
			return;

		int myGroupId = group.GetGroupID();

		for (int i = m_aRequests.Count() - 1; i >= 0; i--)
		{
			ACM_SquadLinkRequest request = m_aRequests[i];
			if (request && request.Matches(requesterGroupId, myGroupId))
				m_aRequests.Remove(i);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Requests aimed at one group. Server-side read, used to build the answer sent to that leader.
	int GetIncomingRequests(int forGroupId, out notnull array<ref ACM_SquadLinkRequest> outRequests)
	{
		outRequests.Clear();

		float now = System.GetTickCount() / 1000.0;
		PruneRequests(now);

		foreach (ACM_SquadLinkRequest request : m_aRequests)
		{
			if (request && request.targetGroupId == forGroupId)
				outRequests.Insert(request);
		}

		return outRequests.Count();
	}

	//------------------------------------------------------------------------------------------------
	protected void PruneRequests(float now)
	{
		for (int i = m_aRequests.Count() - 1; i >= 0; i--)
		{
			ACM_SquadLinkRequest request = m_aRequests[i];
			if (!request || request.IsExpired(now, m_fRequestLifetime))
				m_aRequests.Remove(i);
		}
	}
}
