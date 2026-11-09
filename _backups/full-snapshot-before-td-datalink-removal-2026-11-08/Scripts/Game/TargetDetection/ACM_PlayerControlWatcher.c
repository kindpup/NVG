//------------------------------------------------------------------------------------------------
// ACM_PlayerControlWatcher.c
// Tells target-detection devices when the local player starts controlling a different body,
// and registers the squad / system menu keybinds.
//
// Authority over a device means "is this the helmet of the character I am controlling right now",
// which is not answered once at spawn. A GM takes over an AI, gets unpossessed, drops into another
// unit. Without this the answer latches on the first body, so the wearer's own helmet keeps obeying
// a player who has left it and the body they are in obeys nobody.
//
// Forwards an event and holds no state. What authority means lives on the device component.
//------------------------------------------------------------------------------------------------

modded class SCR_PlayerController
{
	//------------------------------------------------------------------------------------------------
	override void OnControlledEntityChanged(IEntity from, IEntity to)
	{
		super.OnControlledEntityChanged(from, to);

		// On a listen server this runs for every player's controller in the same process, so
		// reacting to all of them would move authority on somebody else's possession.
		if (this != GetGame().GetPlayerController())
			return;

		ACM_TargetDetectionComponent.OnLocalControlledEntityChanged();

		// Squad command keybind, registered from here so it needs no prefab entry.
		//
		// Idempotent - it attaches on the first call that finds an input manager and does nothing
		// afterwards. Driven from this event rather than from init because on a client the local
		// controller and the input manager are not both ready when components are built, and a
		// listener that fails to attach once fails silently for the whole session.
		ACM_SquadMenuOpener.EnsureRegistered();

		// The five per-system menus register the same way and for the same reasons.
		ACM_SystemMenuOpener.EnsureRegistered();
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
	}
}
