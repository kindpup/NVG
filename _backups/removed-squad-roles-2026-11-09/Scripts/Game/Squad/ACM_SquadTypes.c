//------------------------------------------------------------------------------------------------
// ACM_SquadTypes.c
// Value types for squad command.
//------------------------------------------------------------------------------------------------

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
