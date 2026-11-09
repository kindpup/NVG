//------------------------------------------------------------------------------------------------
// ACM_SquadMenuOpener.c
// Owns the ACM_Squad_Menu keybind and nothing else.
//
//------------------------------------------------------------------------------------------------
// NOT A COMPONENT, DELIBERATELY.
//
// The first version of this was a ScriptComponent that had to be added to the player controller
// prefab by hand. That is a silent failure mode: with the component missing, the action is bound,
// the key appears in the settings menu, pressing it does nothing, and there is no error anywhere to
// explain why. It cost exactly that once already.
//
// This mod already declares `modded class SCR_PlayerController` in ACM_IvasControlWatcher.c, so
// there is a hook that runs on every player controller with no prefab work required. Registration
// is driven from there instead. Nothing to add in Workbench, nothing to forget.
//
// Registration is idempotent and local-only: every controller in a listen server's process runs the
// same modded class, and a listener attached from a remote one would open the menu on the wrong
// machine - the same trap the camera handler and the animation path both hit earlier in this mod.
//------------------------------------------------------------------------------------------------
class ACM_SquadMenuOpener
{
	// Temporary. Says whether the bind is live, because "nothing happens" has two very different
	// causes - no listener, or a listener that fired and the menu refused to open - and they are
	// indistinguishable without a line of log.
	protected static const bool ACM_SQUAD_DEBUG = false;

	protected static bool s_bRegistered;

	// The same press can arrive more than once - the menu actions are live in the character contexts
	// AND in ACM_MenuContext so that the key still works while a menu is up, and a toggle that fires
	// twice opens and closes in one frame. Anything inside this window is the same press.
	protected static const int TOGGLE_DEBOUNCE_MS = 250;
	protected static int s_iLastToggleTick;

	//------------------------------------------------------------------------------------------------
	protected static bool DebouncedOut()
	{
		int now = System.GetTickCount();

		if (s_iLastToggleTick != 0 && now - s_iLastToggleTick < TOGGLE_DEBOUNCE_MS)
			return true;

		s_iLastToggleTick = now;
		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! Safe to call as often as you like. Attaches on the first call that finds a local controller
	//! and an input manager, and does nothing on every call after that.
	static void EnsureRegistered()
	{
		if (s_bRegistered)
			return;

		if (!GetGame())
			return;

		// Local player only.
		PlayerController controller = GetGame().GetPlayerController();
		if (!controller)
			return;

		InputManager input = GetGame().GetInputManager();
		if (!input)
			return;

		input.AddActionListener("ACM_Squad_Menu", EActionTrigger.DOWN, OnSquadMenuInput);
		s_bRegistered = true;

		if (ACM_SQUAD_DEBUG)
			Print("[ACM Squad] keybind listener registered for ACM_Squad_Menu.", LogLevel.DEBUG);
	}

	//------------------------------------------------------------------------------------------------
	static void Unregister()
	{
		if (!s_bRegistered)
			return;

		if (GetGame() && GetGame().GetInputManager())
			GetGame().GetInputManager().RemoveActionListener("ACM_Squad_Menu", EActionTrigger.DOWN, OnSquadMenuInput);

		s_bRegistered = false;
	}

	//------------------------------------------------------------------------------------------------
	// Toggle rather than open. A key that only opens leaves the player hunting for Escape, and a
	// second press is what everyone tries first.
	//------------------------------------------------------------------------------------------------
	protected static void OnSquadMenuInput()
	{
		if (DebouncedOut())
			return;

		if (ACM_SQUAD_DEBUG)
			Print("[ACM Squad] ACM_Squad_Menu pressed.", LogLevel.DEBUG);

		MenuManager menus = GetGame().GetMenuManager();
		if (!menus)
		{
			if (ACM_SQUAD_DEBUG)
				Print("[ACM Squad] no MenuManager - cannot open.", LogLevel.WARNING);

			return;
		}

		MenuBase open = menus.FindMenuByPreset(ChimeraMenuPreset.ACM_SquadMenu);
		if (open)
		{
			menus.CloseMenu(open);

			if (ACM_SQUAD_DEBUG)
				Print("[ACM Squad] menu was open - closing.", LogLevel.DEBUG);

			return;
		}

		// Refuse while another menu is up. Opening a second menu over an inventory or a map is how
		// input contexts end up stacked and the player loses their cursor.
		if (menus.IsAnyMenuOpen())
		{
			if (ACM_SQUAD_DEBUG)
				Print("[ACM Squad] another menu is already open - refusing.", LogLevel.DEBUG);

			return;
		}

		MenuBase opened = menus.OpenMenu(ChimeraMenuPreset.ACM_SquadMenu);

		if (ACM_SQUAD_DEBUG)
		{
			if (opened)
				Print("[ACM Squad] menu opened.", LogLevel.DEBUG);
			else
				Print("[ACM Squad] OpenMenu returned null. The MenuPreset is not registered, or its layout failed to load - check Configs/System/chimeraMenus.conf and the layout GUID.", LogLevel.WARNING);
		}
	}
}
