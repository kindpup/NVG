//------------------------------------------------------------------------------------------------
// ACM_PlayerControlWatcher.c
// Registers the squad / system menu keybinds once the local player controller and input manager
// are ready.
//------------------------------------------------------------------------------------------------

modded class SCR_PlayerController
{
	//------------------------------------------------------------------------------------------------
	override void OnControlledEntityChanged(IEntity from, IEntity to)
	{
		super.OnControlledEntityChanged(from, to);

		// On a listen server this runs for every player's controller in the same process.
		if (this != GetGame().GetPlayerController())
			return;

		// Squad command keybind, registered from here so it needs no prefab entry.
		//
		// Idempotent - it attaches on the first call that finds an input manager and does nothing
		// afterwards. Driven from this event rather than from init because on a client the local
		// controller and the input manager are not both ready when components are built, and a
		// listener that fails to attach once fails silently for the whole session.
		ACM_SquadMenuOpener.EnsureRegistered();

		// The per-system menus register the same way and for the same reasons.
		ACM_SystemMenuOpener.EnsureRegistered();

		// Remote Feed toggle / cycle keys.
		ACM_RemoteFeedInput.EnsureRegistered();
	}

	//------------------------------------------------------------------------------------------------
	// Second chance at registration.
	//
	// OnControlledEntityChanged does not fire on every path into a session - a client that spawns
	// straight into a body may never see it - so init gets a deferred attempt as well. Both call the
	// same idempotent method, so whichever wins, the listener is attached exactly once.
	//------------------------------------------------------------------------------------------------
	override void OnInit(IEntity owner)
	{
		super.OnInit(owner);

		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().CallLater(ACM_TrySquadBind, 2000, true);
	}

	//------------------------------------------------------------------------------------------------
	protected void ACM_TrySquadBind()
	{
		if (this != GetGame().GetPlayerController())
			return;

		ACM_SquadMenuOpener.EnsureRegistered();
		ACM_SystemMenuOpener.EnsureRegistered();
		ACM_RemoteFeedInput.EnsureRegistered();
	}
}
