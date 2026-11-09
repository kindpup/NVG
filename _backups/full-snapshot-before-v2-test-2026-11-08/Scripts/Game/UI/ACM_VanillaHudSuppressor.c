//------------------------------------------------------------------------------------------------
// ACM_VanillaHudSuppressor.c
// Hides the ordinary game HUD while the IVAS visor owns the screen, and puts it back afterwards.
//
// THE ONE RULE: RESTORE EXACTLY WHAT WAS HIDDEN, AND NOTHING ELSE.
//
// Every display this suppresses is recorded first, along with whether it was even visible at the
// time. Restoring walks that list and no other, so an element the game had already hidden for its
// own reasons - a display gated on being in a vehicle, something a different mod switched off -
// stays hidden. Blanket-showing everything on the way out would "fix" the screen by turning on
// things nobody asked for, and that failure is invisible until somebody notices a HUD element that
// should not be there.
//
// This is the same discipline the camera handler needed and originally lacked: a switch has no idea
// what state it interrupted unless it writes it down.
//
// SCR_InfoDisplay.Show is the sanctioned call rather than poking widget visibility - the engine
// documents it as hiding "properly, not breaking other systems like AdaptiveOpacity or
// ConditionalVisibility". Reaching past it to ClearFlags would repeat the exact mistake that made
// devices vanish out of the inventory preview.
//
// A BLOCK LIST, NOT A TAKEOVER. It would be easy to hide every display and show a clean visor, and
// it would also hide the respawn screen, the death UI, interaction prompts and the map. So the set
// is authored on the device and defaults to what a visor genuinely obscures. Widening it is one
// string edit; recovering from a player who cannot find the respawn button is not.
//------------------------------------------------------------------------------------------------
class ACM_HiddenDisplayRecord
{
	SCR_InfoDisplay m_Display;
	bool m_bWasShown;
}

class ACM_VanillaHudSuppressor
{
	// TEMPORARILY ON. Logs every registered HUD display and whether the filter caught it.
	//
	// The concrete display classes are not all in the public API index and vary with scenario and
	// installed mods, so the only honest way to build the block list is to read what is actually
	// registered on a live HUD rather than guess at names. One run of this produces the real list.
	// Turn it off once the defaults are settled.
	protected static const bool ACM_HUD_DEBUG = false;

	protected static ref array<ref ACM_HiddenDisplayRecord> s_aHidden = new array<ref ACM_HiddenDisplayRecord>();
	protected static bool s_bChatHidden;
	protected static bool s_bActive;

	//------------------------------------------------------------------------------------------------
	// classFilter is a comma-separated list of class-name fragments. A display whose type name
	// contains any of them is hidden. Fragments rather than exact names on purpose: the concrete
	// classes vary between scenarios and mods, and "Chat" catching three chat-ish displays is the
	// behaviour wanted rather than a bug.
	//------------------------------------------------------------------------------------------------
	static void Suppress(string classFilter, bool hideChat)
	{
		if (s_bActive)
			return;

		if (!GetGame() || !GetGame().GetWorkspace())
			return;

		SCR_HUDManagerComponent hud = SCR_HUDManagerComponent.GetHUDManager();
		if (!hud)
			return;

		array<string> fragments = {};
		ParseFilter(classFilter, fragments);

		array<SCR_InfoDisplay> elements = hud.GetHUDElements();
		if (elements)
		{
			if (ACM_HUD_DEBUG)
				Print("[ACM HUD] ---- " + elements.Count().ToString() + " registered display(s). HIDE = caught by the filter ----", LogLevel.DEBUG);

			foreach (SCR_InfoDisplay display : elements)
			{
				if (!display)
					continue;

				bool matched = Matches(display, fragments);

				if (ACM_HUD_DEBUG)
				{
					string mark = "     ";
					if (matched)
						mark = "HIDE ";

					string shown = " (already hidden)";
					if (display.IsShown())
						shown = "";

					Print("[ACM HUD] " + mark + DisplayName(display) + shown + "   layout: " + display.m_LayoutPath, LogLevel.DEBUG);
				}

				if (!matched)
					continue;

				ACM_HiddenDisplayRecord record = new ACM_HiddenDisplayRecord();
				record.m_Display = display;
				record.m_bWasShown = display.IsShown();
				s_aHidden.Insert(record);

				// Only touch what was actually up. Hiding something already hidden would still be
				// recorded, and would then be shown on restore - turning a suppressor into a
				// revealer.
				if (record.m_bWasShown)
					display.Show(false, UIConstants.FADE_RATE_FAST);
			}
		}

		if (hideChat)
		{
			SCR_ChatPanelManager chat = SCR_ChatPanelManager.GetInstance();
			if (chat)
			{
				chat.HideAllChatPanels();
				s_bChatHidden = true;
			}
		}

		s_bActive = true;
	}

	//------------------------------------------------------------------------------------------------
	static void Restore()
	{
		if (!s_bActive)
			return;

		foreach (ACM_HiddenDisplayRecord record : s_aHidden)
		{
			// Was not visible when we arrived, so it is not ours to turn on.
			if (!record || !record.m_Display || !record.m_bWasShown)
				continue;

			record.m_Display.Show(true, UIConstants.FADE_RATE_FAST);
		}

		s_aHidden.Clear();

		if (s_bChatHidden)
		{
			SCR_ChatPanelManager chat = SCR_ChatPanelManager.GetInstance();
			if (chat)
				chat.ShowAllChatPanels();

			s_bChatHidden = false;
		}

		s_bActive = false;
	}

	//------------------------------------------------------------------------------------------------
	static bool IsSuppressed()
	{
		return s_bActive;
	}

	//------------------------------------------------------------------------------------------------
	protected static string DisplayName(SCR_InfoDisplay display)
	{
		typename type = display.Type();
		if (!type)
			return "<no type>";

		return type.ToString();
	}

	//------------------------------------------------------------------------------------------------
	// Class name OR layout path, because the class name is not always distinguishing.
	//
	// A live HUD reports two separate displays as bare SCR_InfoDisplayExtended - they are different
	// UI built on the same base, and nothing in the type name separates them. Their layout paths do,
	// so a fragment can target either axis and the awkward cases become addressable without
	// resorting to hiding a base class and taking its siblings with it.
	protected static bool Matches(SCR_InfoDisplay display, array<string> fragments)
	{
		if (fragments.IsEmpty())
			return false;

		typename type = display.Type();
		if (type)
		{
			string name = type.ToString();
			name.ToLower();

			foreach (string fragment : fragments)
			{
				if (name.Contains(fragment))
					return true;
			}
		}

		string layout = display.m_LayoutPath;
		if (layout.IsEmpty())
			return false;

		layout.ToLower();

		foreach (string pathFragment : fragments)
		{
			if (layout.Contains(pathFragment))
				return true;
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected static void ParseFilter(string configured, out notnull array<string> outFragments)
	{
		array<string> parts = {};
		configured.Split(",", parts, true);

		foreach (string part : parts)
		{
			string trimmed = part;
			trimmed.Trim();
			trimmed.ToLower();

			if (!trimmed.IsEmpty())
				outFragments.Insert(trimmed);
		}
	}
}
