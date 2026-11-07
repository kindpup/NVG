//------------------------------------------------------------------------------------------------
// ACM_PlayerNames.c
// One place this mod resolves a player's display name.
//
//------------------------------------------------------------------------------------------------
// WHY THIS EXISTS
//
// Everything here used to call PlayerManager.GetPlayerName(), which returns the PLATFORM name - the
// Steam or Xbox gamertag - and nothing else. A server running custom nametags therefore showed the
// custom name in chat, on the vanilla nametag and in the scoreboard, and the gamertag on every ACM
// readout: the IVAS squad strip, the contact log, the remote feed operator line, the squad menu
// roster. The same person, two different names, depending on which panel you looked at.
//
//------------------------------------------------------------------------------------------------
// HOW IT IS DONE, AND WHY IT IS NOT A DEPENDENCY
//
// SCR_PlayerNamesFilterCache.GetPlayerDisplayName() is the VANILLA display-name path - the one the
// base game itself uses when it needs a name for a human to read. Shadow Admin overrides exactly
// that method:
//
//     modded class SCR_PlayerNamesFilterCache
//     {
//         override string GetPlayerDisplayName(int playerId)
//         {
//             string stockName = super.GetPlayerDisplayName(playerId);
//             ... return the Shadow Haven nametag if this player has one ...
//         }
//     }
//
// So asking the vanilla cache is all this mod has to do. With Shadow Admin loaded the custom name
// comes back; without it the platform name comes back; and ACM names nothing, links to nothing, and
// needs no compatibility check. Any other mod that patches the same vanilla method works too,
// automatically, because none of them are named here.
//
// The alternative - calling SH_NametagAuthorityComponent directly - would have been a hard
// reference to another mod's class, which fails to compile the moment that mod is not in the load
// order. That is the change to refuse in review.
//
// GetPlayerName() remains correct for one thing only: matching an identity, where the platform name
// is the stable value and a custom name is not. Nothing in this mod does that today.
//------------------------------------------------------------------------------------------------
class ACM_PlayerNames
{
	//------------------------------------------------------------------------------------------------
	//! The name a human should be shown for this player. Custom nametag where one exists, platform
	//! name otherwise, empty string if the player cannot be resolved at all.
	static string GetDisplayName(int playerId)
	{
		if (playerId <= 0)
			return string.Empty;

		PlayerManager players = GetGame().GetPlayerManager();
		if (!players)
			return string.Empty;

		// IS THIS A REAL, CONNECTED PLAYER?
		//
		// Asked BEFORE the display cache, not after. A positive id is not the same as a connected
		// player - a group can carry ids for players who have left, and an editor session hands out
		// ids that never belonged to anyone. The vanilla display path runs those through
		// SocialComponent.IsRestricted, which logs "Invalid otherPlayerID" for every one of them,
		// once per lookup, twice a second for as long as a panel is open.
		//
		// GetPlayerName is the cheap identity check: empty means the id resolves to nobody, so
		// there is no name to show and nothing to gain by asking further.
		string platform = players.GetPlayerName(playerId);
		if (platform.IsEmpty())
			return string.Empty;

		// Vanilla display path. This is the seam custom-name mods override.
		SCR_PlayerNamesFilterCache cache = SCR_PlayerNamesFilterCache.GetInstance();
		if (cache)
		{
			string display = cache.GetPlayerDisplayName(playerId);
			if (!display.IsEmpty())
				return display;
		}

		// Cache absent - a dedicated server without UI, or very early in a session. Degrade to the
		// platform name rather than to a blank.
		return platform;
	}

	//------------------------------------------------------------------------------------------------
	//! Uppercased, for the IVAS readouts that are set in caps. Kept here rather than at each call
	//! site so the mod cannot end up uppercasing in some panels and not others.
	static string GetDisplayNameUpper(int playerId)
	{
		string name = GetDisplayName(playerId);
		if (name.IsEmpty())
			return name;

		name.ToUpper();

		return name;
	}

	//------------------------------------------------------------------------------------------------
	//! Display name, or the supplied fallback when the player cannot be resolved.
	static string GetDisplayNameOr(int playerId, string fallback)
	{
		string name = GetDisplayName(playerId);
		if (name.IsEmpty())
			return fallback;

		return name;
	}
}
