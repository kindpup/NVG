//------------------------------------------------------------------------------------------------
// ACM_SquadTypes.c
// Value types for squad command and cross-group data link sharing.
//
// THE AUTHORITY MODEL, stated once here because every file below depends on it.
//
// ACM_TacticalContactManager says of its explicit links: "Server-side entry points only. There is
// no RPC that reaches these, and there must never be one: a client able to grant itself a link to
// another squad's feed would make every scope setting above decorative."
//
// That rule is preserved, not broken. A leader is given exactly two verbs, and neither of them can
// take anything:
//
//   OPEN     A leader shares THEIR OWN group's picture outward. They are giving away data they
//            already own, which needs nobody else's consent and grants them nothing.
//
//   REQUEST  A leader asks another group for ITS picture. This creates a pending request and
//            nothing else. It confers no access whatsoever until that group's own leader approves.
//
// APPROVE is the only path that results in one group seeing another's contacts, and it can only be
// performed by the leader of the group being asked. So a client still cannot grant itself a link -
// the thing the original comment forbids - and the invariant holds in substance rather than only in
// letter.
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
// Direction of a share, from the perspective of the group that owns the data.
//------------------------------------------------------------------------------------------------
enum ACM_ESquadLinkState
{
	// No relationship at all.
	NONE,
	// This group has asked another for its picture and is waiting on that leader.
	PENDING_OUTGOING,
	// Another group has asked for this group's picture and this leader has not answered.
	PENDING_INCOMING,
	// Live. The receiving group is seeing the sharing group's contacts.
	ACTIVE
}

//------------------------------------------------------------------------------------------------
// One pending ask for a group's picture.
//
// Deliberately carries no access of any kind. This record existing is exactly as permissive as it
// not existing - the only thing that opens a feed is the target leader turning it into a grant.
//------------------------------------------------------------------------------------------------
class ACM_SquadLinkRequest
{
	// The group that wants to receive.
	int requesterGroupId;

	// The group being asked to share.
	int targetGroupId;

	// Who asked, for display and for the audit line. Not used for any permission decision - the
	// decision is made from live group membership at the moment of approval, never from this.
	int requesterPlayerId;

	// Seconds, same clock as the rest of the system. Requests go stale rather than queuing forever.
	float createdAt;

	//------------------------------------------------------------------------------------------------
	void ACM_SquadLinkRequest(int fromGroup, int toGroup, int fromPlayer, float now)
	{
		requesterGroupId = fromGroup;
		targetGroupId = toGroup;
		requesterPlayerId = fromPlayer;
		createdAt = now;
	}

	//------------------------------------------------------------------------------------------------
	bool IsExpired(float now, float lifetimeSeconds)
	{
		if (lifetimeSeconds <= 0)
			return false;

		return (now - createdAt) > lifetimeSeconds;
	}

	//------------------------------------------------------------------------------------------------
	bool Matches(int fromGroup, int toGroup)
	{
		return requesterGroupId == fromGroup && targetGroupId == toGroup;
	}
}

//------------------------------------------------------------------------------------------------
// One row of the squad roster, as the menu displays it.
//
// Built fresh each time the menu refreshes rather than cached, because a roster that lags behind
// the group is a roster that assigns a role to somebody who has left.
//------------------------------------------------------------------------------------------------
class ACM_SquadMember
{
	int playerId;
	string name;
	ACM_ETacticalRole role;
	bool isLeader;
	bool isLocal;

	//------------------------------------------------------------------------------------------------
	void ACM_SquadMember(int player, string playerName, ACM_ETacticalRole tacticalRole, bool leader, bool local)
	{
		playerId = player;
		name = playerName;
		role = tacticalRole;
		isLeader = leader;
		isLocal = local;
	}
}
