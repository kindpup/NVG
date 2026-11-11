//
//  ACM
//  ACM Loadout Replay Compatibility Guard
//
//  Original ACM-owned script for the modular framework.
//==================================================================================================

/*!
	Short-lived guard for vanilla arsenal/Bacon Loadout Editor serialized
	loadout replay. This must not disable ACM routing globally. Its only job is
	to suppress deferred mutation helpers such as visible-slot refill and
	family rebound while the engine is rebuilding an explicit saved slot graph.
*/
class ACM_LoadoutReplayCompat
{
	protected static int s_iReplayWindowDepth;
	static const int ACM_REPLAY_WINDOW_MS = 250;

	static void MarkReplayWindow()
	{
		s_iReplayWindowDepth++;

		if (GetGame())
			GetGame().GetCallqueue().CallLater(ClearReplayWindow, ACM_REPLAY_WINDOW_MS, false);
	}

	static bool IsReplayWindowActive()
	{
		return s_iReplayWindowDepth > 0;
	}

	static void ClearReplayWindow()
	{
		if (s_iReplayWindowDepth > 0)
			s_iReplayWindowDepth--;
	}
}

