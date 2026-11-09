//------------------------------------------------------------------------------------------------
// ACM_ExternalHudSuppression.c
// The seam a compatibility addon uses to tell ACM IVAS that something else owns the screen.
//
// This file exists so that the boundary is visible. ACM IVAS core does not know that other camera
// mods exist, cannot name one, and must never gain a reference to one - the moment a class name
// from another addon appears in core, that addon becomes a load-order dependency for everybody.
//
// So the arrangement is: core exposes a switch and a reason, and knows nothing about who threw it.
// A compatibility addon - a separate mod, depending on both sides - watches whatever it needs to
// watch and calls Suppress/Release. If that addon is not installed, nothing here ever fires and the
// feed behaves exactly as it always has.
//
// Deliberately NOT a stack of claims. One switch, last caller wins, because two compatibility
// addons fighting over the screen is a configuration problem to notice rather than a state machine
// to arbitrate.
//------------------------------------------------------------------------------------------------
class ACM_ExternalHudSuppression
{
	//------------------------------------------------------------------------------------------------
	// Take the screen. The feed remembers whether the wearer had it up and puts it back on release,
	// so a suppression that comes and goes is invisible to the player.
	//
	// The reason string is for logs and for the feed's own status line. Keep it short and say who
	// is asking - "native dog camera" reads better in a log than "suppressed".
	//------------------------------------------------------------------------------------------------
	static void Suppress(string reason)
	{
		ACM_RemoteFeedRenderer.SetExternallySuppressed(true, reason);
	}

	//------------------------------------------------------------------------------------------------
	static void Release()
	{
		ACM_RemoteFeedRenderer.SetExternallySuppressed(false, string.Empty);
	}

	//------------------------------------------------------------------------------------------------
	static bool IsSuppressed()
	{
		return ACM_RemoteFeedRenderer.IsExternallySuppressed();
	}

	//------------------------------------------------------------------------------------------------
	static string GetReason()
	{
		return ACM_RemoteFeedRenderer.GetSuppressionReason();
	}
}
