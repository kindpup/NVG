//------------------------------------------------------------------------------------------------
// ACM_SystemMenuOpener.c
// Owns the five system-menu keybinds and nothing else.
//
// Same shape as ACM_SquadMenuOpener and for the same reason: not a ScriptComponent, because a
// component that has to be added to the player controller prefab by hand fails silently when it is
// missing - the action binds, the key shows in settings, pressing it does nothing, and no error is
// logged anywhere to say why. Registration is driven from the modded SCR_PlayerController instead,
// so there is nothing to add in Workbench and nothing to forget.
//
// One static callback per action because an InputManager listener takes a bare function reference
// and cannot carry the preset with it.
//------------------------------------------------------------------------------------------------
class ACM_SystemMenuOpener
{
	// Temporary, matching the squad opener. "Nothing happens" has two very different causes - no
	// listener at all, or a listener that fired and the menu refused to open - and without a line of
	// log they are indistinguishable.
	protected static const bool ACM_SYSMENU_DEBUG = false;

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

		// Local player only. Every controller in a listen server's process runs the same modded
		// class, and a listener attached from a remote one would open a menu on the wrong machine.
		PlayerController controller = GetGame().GetPlayerController();
		if (!controller)
			return;

		InputManager input = GetGame().GetInputManager();
		if (!input)
			return;

		input.AddActionListener("ACM_Lights_Menu", EActionTrigger.DOWN, OnLightsMenuInput);
		input.AddActionListener("ACM_Lasers_Menu", EActionTrigger.DOWN, OnLasersMenuInput);
		input.AddActionListener("ACM_NVG_Menu", EActionTrigger.DOWN, OnNVGMenuInput);
		input.AddActionListener("ACM_Thermal_Menu", EActionTrigger.DOWN, OnThermalMenuInput);
		input.AddActionListener("ACM_IVAS_Menu", EActionTrigger.DOWN, OnIvasMenuInput);

		s_bRegistered = true;

		if (ACM_SYSMENU_DEBUG)
			Print("[ACM Menus] keybind listeners registered for the five system menus.", LogLevel.DEBUG);
	}

	//------------------------------------------------------------------------------------------------
	static void Unregister()
	{
		if (!s_bRegistered)
			return;

		if (GetGame() && GetGame().GetInputManager())
		{
			InputManager input = GetGame().GetInputManager();
			input.RemoveActionListener("ACM_Lights_Menu", EActionTrigger.DOWN, OnLightsMenuInput);
			input.RemoveActionListener("ACM_Lasers_Menu", EActionTrigger.DOWN, OnLasersMenuInput);
			input.RemoveActionListener("ACM_NVG_Menu", EActionTrigger.DOWN, OnNVGMenuInput);
			input.RemoveActionListener("ACM_Thermal_Menu", EActionTrigger.DOWN, OnThermalMenuInput);
			input.RemoveActionListener("ACM_IVAS_Menu", EActionTrigger.DOWN, OnIvasMenuInput);
		}

		s_bRegistered = false;
	}

	//------------------------------------------------------------------------------------------------
	protected static void OnLightsMenuInput() { Toggle(ChimeraMenuPreset.ACM_LightsMenu, "Lights"); }
	protected static void OnLasersMenuInput() { Toggle(ChimeraMenuPreset.ACM_LasersMenu, "Lasers"); }
	protected static void OnNVGMenuInput() { Toggle(ChimeraMenuPreset.ACM_NVGMenu, "Night Vision"); }
	protected static void OnThermalMenuInput() { Toggle(ChimeraMenuPreset.ACM_ThermalMenu, "Thermal"); }
	protected static void OnIvasMenuInput() { Toggle(ChimeraMenuPreset.ACM_IvasMenu, "IVAS"); }

	//------------------------------------------------------------------------------------------------
	// Toggle rather than open. A key that only opens leaves the player hunting for Escape, and a
	// second press is what everyone tries first.
	//
	// A different ACM system menu already being up is not a refusal - it is a switch. Closing it and
	// opening the requested one is what a player pressing the thermal key while the NVG menu is up
	// actually means, and refusing would make the key look broken.
	//------------------------------------------------------------------------------------------------
	protected static void Toggle(ChimeraMenuPreset preset, string label)
	{
		if (DebouncedOut())
			return;

		if (ACM_SYSMENU_DEBUG)
			Print("[ACM Menus] " + label + " menu key pressed.", LogLevel.DEBUG);

		MenuManager menus = GetGame().GetMenuManager();
		if (!menus)
		{
			if (ACM_SYSMENU_DEBUG)
				Print("[ACM Menus] no MenuManager - cannot open.", LogLevel.WARNING);

			return;
		}

		MenuBase open = menus.FindMenuByPreset(preset);
		if (open)
		{
			menus.CloseMenu(open);

			if (ACM_SYSMENU_DEBUG)
				Print("[ACM Menus] " + label + " menu was open - closing.", LogLevel.DEBUG);

			return;
		}

		CloseOtherSystemMenus(menus, preset);

		// Refuse while an unrelated menu is up. Opening over an inventory or a map is how input
		// contexts end up stacked and the player loses their cursor.
		if (menus.IsAnyMenuOpen())
		{
			if (ACM_SYSMENU_DEBUG)
				Print("[ACM Menus] another menu is already open - refusing.", LogLevel.DEBUG);

			return;
		}

		MenuBase opened = menus.OpenMenu(preset);

		if (ACM_SYSMENU_DEBUG)
		{
			if (opened)
				Print("[ACM Menus] " + label + " menu opened.", LogLevel.DEBUG);
			else
				Print("[ACM Menus] OpenMenu returned null for the " + label + " menu. The MenuPreset is not registered, or its layout failed to load - check Configs/System/chimeraMenus.conf and that UI/layouts/Menus/ACM_SystemMenu.layout has been imported.", LogLevel.WARNING);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static void CloseOtherSystemMenus(notnull MenuManager menus, ChimeraMenuPreset keep)
	{
		CloseIfOpen(menus, ChimeraMenuPreset.ACM_LightsMenu, keep);
		CloseIfOpen(menus, ChimeraMenuPreset.ACM_LasersMenu, keep);
		CloseIfOpen(menus, ChimeraMenuPreset.ACM_NVGMenu, keep);
		CloseIfOpen(menus, ChimeraMenuPreset.ACM_ThermalMenu, keep);
		CloseIfOpen(menus, ChimeraMenuPreset.ACM_IvasMenu, keep);
	}

	//------------------------------------------------------------------------------------------------
	protected static void CloseIfOpen(notnull MenuManager menus, ChimeraMenuPreset preset, ChimeraMenuPreset keep)
	{
		if (preset == keep)
			return;

		MenuBase other = menus.FindMenuByPreset(preset);
		if (other)
			menus.CloseMenu(other);
	}
}
