//------------------------------------------------------------------------------------------------
// ACM_SelectionMenuEntryAccess.c
// Two public doors onto SCR_SelectionMenuEntry, which vanilla declares protected.
//
// The radial menu can perform an entry and read its label because SCR_RadialMenu is inside the
// family. A list menu is not, so without this it would have to reimplement every action and every
// label a second time - and the moment there are two implementations of "turn the flashlight on"
// they start disagreeing about what it does and what it is called.
//
// Additive only. No vanilla behaviour is modified and nothing is overridden. Each method does one
// thing: call the protected method from inside the class that owns it.
//------------------------------------------------------------------------------------------------
modded class SCR_SelectionMenuEntry
{
	//------------------------------------------------------------------------------------------------
	// Perform this entry from outside the radial menu. Same call the radial makes, same guards, same
	// result - the only difference is who is allowed to ask.
	//------------------------------------------------------------------------------------------------
	void ACM_PerformFromMenu()
	{
		OnPerform();
	}

	//------------------------------------------------------------------------------------------------
	// The entry's label, recomputed against live state first.
	//
	// Update() is what every ACM entry class uses to fold current hardware state into its name -
	// "Flashlight [ON]", "IVAS [OFF]", "Data Link [SHARING]". Calling it here means a list menu
	// shows exactly what the radial would show for the same entry, with no second label table to
	// drift out of step.
	//------------------------------------------------------------------------------------------------
	string ACM_MenuLabel()
	{
		Update();

		return GetName();
	}
}
