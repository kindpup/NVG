//------------------------------------------------------------------------------------------------
// ACM_SquadLinkRegistry.c
// Server-side list of squad-to-squad links made from the squad menu (open / approve). The remote
// feed's EXPLICIT_LINK policy reads it to decide whether a viewer in another squad may watch a feed.
//
// Server-only data, and there is deliberately no RPC that writes to it: ACM_SquadCommandComponent
// validates leadership on the server before it grants or revokes anything.
//------------------------------------------------------------------------------------------------

class ACM_SquadLinkGrant
{
	int fromGroupId;
	int toGroupId;

	// When true the link runs both ways.
	bool bidirectional;

	// Absolute time the link lapses, or -1 for one that lasts the session.
	float expiresAt;

	//------------------------------------------------------------------------------------------------
	void ACM_SquadLinkGrant(int from, int to, bool both, float expiry)
	{
		fromGroupId = from;
		toGroupId = to;
		bidirectional = both;
		expiresAt = expiry;
	}

	//------------------------------------------------------------------------------------------------
	bool Covers(int producerGroupId, int consumerGroupId)
	{
		if (fromGroupId == producerGroupId && toGroupId == consumerGroupId)
			return true;

		return bidirectional && fromGroupId == consumerGroupId && toGroupId == producerGroupId;
	}

	//------------------------------------------------------------------------------------------------
	bool IsExpired(float now)
	{
		return expiresAt > 0 && now >= expiresAt;
	}
}

class ACM_SquadLinkRegistry
{
	protected static ref array<ref ACM_SquadLinkGrant> s_aGrants = {};

	//------------------------------------------------------------------------------------------------
	//! True if the two players are in the same group, or the first player's group shares to the second's.
	static bool GroupsAreLinked(int fromPlayerId, int toPlayerId)
	{
		int fromGroup = ResolveGroupId(fromPlayerId);
		int toGroup = ResolveGroupId(toPlayerId);

		if (fromGroup <= 0 || toGroup <= 0)
			return false;

		if (fromGroup == toGroup)
			return true;

		float now = System.GetTickCount() / 1000.0;

		foreach (ACM_SquadLinkGrant grant : s_aGrants)
		{
			if (grant && !grant.IsExpired(now) && grant.Covers(fromGroup, toGroup))
				return true;
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	static void ServerGrantLink(int fromGroupId, int toGroupId, bool bidirectional, float durationSeconds)
	{
		if (Replication.IsRunning() && !Replication.IsServer())
			return;

		if (fromGroupId < 0 || toGroupId < 0 || fromGroupId == toGroupId)
			return;

		float expiry = -1;
		if (durationSeconds > 0)
			expiry = (System.GetTickCount() / 1000.0) + durationSeconds;

		// Re-granting an existing pair refreshes it rather than stacking duplicates.
		foreach (ACM_SquadLinkGrant existing : s_aGrants)
		{
			if (existing && existing.fromGroupId == fromGroupId && existing.toGroupId == toGroupId)
			{
				existing.bidirectional = bidirectional;
				existing.expiresAt = expiry;
				return;
			}
		}

		s_aGrants.Insert(new ACM_SquadLinkGrant(fromGroupId, toGroupId, bidirectional, expiry));
	}

	//------------------------------------------------------------------------------------------------
	static void ServerRevokeLink(int fromGroupId, int toGroupId)
	{
		if (Replication.IsRunning() && !Replication.IsServer())
			return;

		for (int i = s_aGrants.Count() - 1; i >= 0; i--)
		{
			ACM_SquadLinkGrant grant = s_aGrants[i];
			if (grant && grant.fromGroupId == fromGroupId && grant.toGroupId == toGroupId)
				s_aGrants.Remove(i);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static int ResolveGroupId(int playerId)
	{
		if (playerId <= 0)
			return -1;

		SCR_GroupsManagerComponent groups = SCR_GroupsManagerComponent.GetInstance();
		if (!groups)
			return -1;

		SCR_AIGroup group = groups.GetPlayerGroup(playerId);
		if (!group)
			return -1;

		return group.GetGroupID();
	}
}
