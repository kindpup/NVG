//------------------------------------------------------------------------------------------------
// ACM_MPDiag.c
// TEMPORARY multiplayer diagnostics for the night vision / thermal on-off path. Lines are tagged
// [ACM MPDIAG] at normal level so they show in client and server logs. Remove before publishing.
//------------------------------------------------------------------------------------------------
class ACM_MPDiag
{
	static const bool ENABLED = true;

	//------------------------------------------------------------------------------------------------
	static void Log(string message)
	{
		if (!ENABLED)
			return;

		string side = "SP";
		if (Replication.IsRunning())
		{
			if (Replication.IsServer())
				side = "SERVER";
			else
				side = "CLIENT";
		}

		Print("[ACM MPDIAG " + side + "] " + message, LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	//! Network roles of an entity: owner / proxy, as the gadget system sees them.
	static string Roles(IEntity entity)
	{
		if (!entity)
			return "null";

		RplComponent rpl = RplComponent.Cast(entity.FindComponent(RplComponent));
		if (!rpl)
			return "no RplComponent";

		return "owner=" + rpl.IsOwner().ToString() + " proxy=" + rpl.IsProxy().ToString();
	}
}
