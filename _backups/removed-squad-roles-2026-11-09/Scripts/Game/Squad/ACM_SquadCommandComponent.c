//------------------------------------------------------------------------------------------------
// ACM_SquadCommandComponent.c
// Server-authoritative squad command: roster roles, group size and group name.
//
// Lives on the GAME MODE, like ACM_TacticalRoleComponent, because
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
// The verbs a leader has - assign a role, resize the group, rename it - are all
// scoped to their own group. There is no method here that lets a leader touch a group they do not
// lead, and adding one would be the change to refuse in review.
//------------------------------------------------------------------------------------------------
class ACM_SquadCommandComponentClass : ScriptComponentClass {}

class ACM_SquadCommandComponent : ScriptComponent
{
	protected static ACM_SquadCommandComponent s_Instance;

	[Attribute("1", UIWidgets.CheckBox, desc: "Allow group leaders to assign tactical roles to other members of their own group. OFF leaves every player setting only their own role, which is the original behaviour.", category: "ACM Squad Command")]
	protected bool m_bAllowLeaderRoleAssign;

	[Attribute("1", UIWidgets.CheckBox, desc: "Allow group leaders to change their group's maximum member count. This drives the VANILLA group system through SCR_AIGroup.SetMaxMembers, so the change is visible everywhere the base game shows group capacity, not just in this menu.", category: "ACM Squad Command")]
	protected bool m_bAllowSquadResize;

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
}
