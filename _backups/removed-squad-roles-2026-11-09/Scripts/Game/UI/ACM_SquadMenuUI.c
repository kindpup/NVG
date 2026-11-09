//------------------------------------------------------------------------------------------------
// ACM_SquadMenuUI.c
// The squad command menu: roster and roles, group size and group name.
//
// A real menu rather than a HUD overlay, because it is a list-editing screen and it needs a cursor.
// Registered as a MenuPreset in Configs/System/chimeraMenus.conf and opened by keybind.
//
//------------------------------------------------------------------------------------------------
// THIS FILE DECIDES NOTHING.
//
// Every button below sends an ask through ACM_PlayerLinkComponent and then waits to be told what
// happened by the same replicated state it was already reading. It runs no permission check that
// matters: the leader tests here exist to grey out controls, not to authorise anything, and a
// modified client that removed them would simply be sending asks the server refuses.
//
// The single rule for editing this file: if you find yourself wanting to change squad state
// directly instead of asking, stop - that is the change that turns a display into an exploit.
//------------------------------------------------------------------------------------------------
// The ACM_SquadMenu preset name is declared with every other ACM preset in ACM_MenuPresets.c.

//------------------------------------------------------------------------------------------------
// Click routing for a plain widget.
//
// Deliberately not built on a vanilla button prefab. Those carry their own layouts and component
// expectations that would have to be matched exactly, and this menu only needs "tell me when this
// was clicked" - which is one handler and no dependencies.
//------------------------------------------------------------------------------------------------
class ACM_SquadClickHandler : ScriptedWidgetEventHandler
{
	protected ACM_SquadMenuUI m_Menu;
	protected string m_sAction;
	protected int m_iIndex;

	// Hover tint. The plate is repainted rather than a second highlight widget being shown, because
	// SetColor is used all over this codebase and needs no extra widgets to keep in sync.
	protected Widget m_wTint;
	protected ref Color m_ColBase;
	protected ref Color m_ColHover;

	//------------------------------------------------------------------------------------------------
	void ACM_SquadClickHandler(ACM_SquadMenuUI menu, string action, int index)
	{
		m_Menu = menu;
		m_sAction = action;
		m_iIndex = index;
	}

	//------------------------------------------------------------------------------------------------
	//! The base colour is passed in rather than read back off the widget, so a row that is repainted
	//! mid-hover still restores to the right shade instead of to whatever it happened to be.
	void SetTint(Widget target, Color base, Color hover)
	{
		m_wTint = target;
		m_ColBase = base;
		m_ColHover = hover;
	}

	//------------------------------------------------------------------------------------------------
	override bool OnMouseEnter(Widget w, int x, int y)
	{
		if (m_wTint && m_ColHover)
			m_wTint.SetColor(m_ColHover);

		return false;
	}

	//------------------------------------------------------------------------------------------------
	override bool OnMouseLeave(Widget w, Widget enterW, int x, int y)
	{
		if (m_wTint && m_ColBase)
			m_wTint.SetColor(m_ColBase);

		return false;
	}

	//------------------------------------------------------------------------------------------------
	// Controller focus, same treatment as the mouse. Without these a gamepad moves an invisible
	// cursor through the list - the row it is on looks identical to every other row.
	//------------------------------------------------------------------------------------------------
	override bool OnFocus(Widget w, int x, int y)
	{
		if (m_wTint && m_ColHover)
			m_wTint.SetColor(m_ColHover);

		return false;
	}

	//------------------------------------------------------------------------------------------------
	override bool OnFocusLost(Widget w, int x, int y)
	{
		if (m_wTint && m_ColBase)
			m_wTint.SetColor(m_ColBase);

		return false;
	}

	//------------------------------------------------------------------------------------------------
	override bool OnClick(Widget w, int x, int y, int button)
	{
		if (m_Menu)
			m_Menu.OnRowClicked(m_sAction, m_iIndex);

		return true;
	}
}

//------------------------------------------------------------------------------------------------
class ACM_SquadMenuUI : ChimeraMenuBase
{
	// Row counts. Fixed widgets rather than dynamic creation, matching how every other panel in this
	// mod is built - the layout owns the widgets and the script only ever fills or hides them.
	protected static const int ROSTER_ROWS = 12;

	protected static const int REFRESH_MS = 500;

	// One row per role in the catalog. The layout ships this many; a catalog longer than this is
	// truncated rather than silently dropping roles off the bottom of a panel that cannot grow.
	protected static const int ROLE_DROP_ROWS = 16;

	protected Widget m_wRoot;
	protected TextWidget m_wGroupName;
	protected TextWidget m_wLeaderState;
	protected TextWidget m_wSizeValue;

	// Group rename. The edit box is only populated when it is NOT being typed in, so a refresh
	// twice a second cannot overwrite half-typed text under the player's cursor.
	protected EditBoxWidget m_wNameEdit;
	protected Widget m_wNameEditBg;
	protected Widget m_wNameSave;
	protected bool m_bNameEditDirty;

	protected ref array<TextWidget> m_aRosterName = {};
	protected ref array<TextWidget> m_aRosterRole = {};
	protected ref array<Widget> m_aRosterButton = {};

	// EVERY BUTTON IN THIS LAYOUT IS TWO WIDGETS.
	//
	// The coloured plate ("...Bg") and the caption on top of it are siblings, not parent and child,
	// so hiding the caption leaves the plate behind. That is what an unused roster row looked like:
	// twelve blank green tiles down the middle of the panel with nothing written on them. The plates
	// are tracked here so they can be shown and hidden in lockstep with their captions.
	// The size stepper, held so it can be hidden outright when it would do nothing. A visible minus
	// and plus with no group behind them is a control that swallows clicks in silence.
	protected Widget m_wSizeMinus;
	protected Widget m_wSizeMinusBg;
	protected Widget m_wSizePlus;
	protected Widget m_wSizePlusBg;

	// Cell plates behind the text fields. Same lockstep rule as the button plates: a plate left
	// visible under an empty row is a box with nothing in it.
	// ROLE PICKER.
	//
	// A fixed panel that is shown and hidden rather than moved to the clicked row - repositioning a
	// widget at runtime means writing slot offsets, and nothing in this project does that. The
	// member's name is written into the panel instead, so it is still obvious who it applies to.
	protected Widget m_wDropShade;
	protected Widget m_wDropCatch;
	protected Widget m_wDropPanel;
	protected Widget m_wDropCancel;
	protected TextWidget m_wDropTitle;
	protected TextWidget m_wDropWho;

	protected ref array<Widget> m_aDropOpt = {};
	protected ref array<TextWidget> m_aDropLabel = {};

	// Role value behind each visible row, parallel to the rows. Clicks index into THIS.
	protected ref array<int> m_aDropRole = {};

	// Who the picker is editing, held as a player id rather than a roster index - the roster is
	// rebuilt twice a second and an index would drift onto whoever took that slot.
	protected int m_iDropPlayerId = -1;

	// BUTTON / PLATE PAIRS.
	//
	// A ButtonWidgetClass with "style blank" paints nothing - its Color is ignored - so every button
	// in this layout is a transparent hit area sitting over an ImageWidget that draws the actual box.
	// The pair is recorded at bind time so the plate can follow the button's visibility without every
	// call site having to know the plate exists.
	protected ref array<Widget> m_aPairButton = {};
	protected ref array<Widget> m_aPairPlate = {};

	protected ref array<Widget> m_aRosterNameBg = {};
	protected ref array<Widget> m_aRosterRoleBg = {};

	protected ref array<Widget> m_aRosterButtonBg = {};

	// Handlers are kept alive here. A ScriptedWidgetEventHandler attached with SetHandler is not
	// owned by the widget, so dropping the reference collects it and the button silently stops
	// responding - which looks exactly like a broken layout.
	protected ref array<ref ACM_SquadClickHandler> m_aHandlers = {};

	// The roster as of the last refresh. Clicks index into THIS, never into a widget's position, so
	// a roster that changed between the draw and the click cannot resolve to the wrong player.
	protected ref array<ref ACM_SquadMember> m_aMembers = {};

	//------------------------------------------------------------------------------------------------
	override void OnMenuOpen()
	{
		super.OnMenuOpen();

		m_wRoot = GetRootWidget();
		if (!m_wRoot)
			return;

		m_wGroupName = TextWidget.Cast(m_wRoot.FindAnyWidget("ACM_SquadGroupName"));
		m_wLeaderState = TextWidget.Cast(m_wRoot.FindAnyWidget("ACM_SquadLeaderState"));
		m_wSizeValue = TextWidget.Cast(m_wRoot.FindAnyWidget("ACM_SquadSizeValue"));

		m_wSizeMinus = BindButton("ACM_SquadSizeMinus", "size-", 0);
		m_wSizeMinusBg = BindButton("ACM_SquadSizeMinusBg", "size-", 0);
		m_wSizePlus = BindButton("ACM_SquadSizePlus", "size+", 0);
		m_wSizePlusBg = BindButton("ACM_SquadSizePlusBg", "size+", 0);

		BindButton("ACM_SquadClose", "close", 0);
		BindButton("ACM_SquadCloseBtn", "close", 0);

		m_aPairButton.Clear();
		m_aPairPlate.Clear();

		m_aRosterName.Clear();
		m_aRosterRole.Clear();
		m_aRosterButton.Clear();
		m_aRosterButtonBg.Clear();
		m_aRosterNameBg.Clear();
		m_aRosterRoleBg.Clear();

		for (int i = 0; i < ROSTER_ROWS; i++)
		{
			m_aRosterName.Insert(TextWidget.Cast(m_wRoot.FindAnyWidget("ACM_SquadName" + i.ToString())));
			m_aRosterRole.Insert(TextWidget.Cast(m_wRoot.FindAnyWidget("ACM_SquadRole" + i.ToString())));
			m_aRosterButton.Insert(BindButton("ACM_SquadRoleBtn" + i.ToString(), "role", i));

			// The plate takes the click too, so a press anywhere on the tile lands rather than only
			// on the glyph.
			m_aRosterButtonBg.Insert(BindButton("ACM_SquadRoleBtnBg" + i.ToString(), "role", i));

			m_aRosterNameBg.Insert(m_wRoot.FindAnyWidget("ACM_SquadNameBg" + i.ToString()));
			m_aRosterRoleBg.Insert(m_wRoot.FindAnyWidget("ACM_SquadRoleBg" + i.ToString()));
		}

		m_wNameEdit = EditBoxWidget.Cast(m_wRoot.FindAnyWidget("ACM_SquadNameEdit"));
		m_wNameEditBg = m_wRoot.FindAnyWidget("ACM_SquadNameEditBg");
		m_wNameSave = BindButton("ACM_SquadNameSave", "name-save", 0);
		m_bNameEditDirty = false;

		BindRoleDrop();

		Refresh();

		if (GetGame().GetCallqueue())
			GetGame().GetCallqueue().CallLater(Refresh, REFRESH_MS, true);
	}


	//------------------------------------------------------------------------------------------------
	// KEEPS THE MENU KEYBIND ALIVE WHILE THE MENU IS UP.
	//
	// ActivateContext has to be pumped every frame - an action in a context that was never activated
	// reads 0 forever, which is the note already written against ACM_ThermalContext in this mod. The
	// menu actions sit in ACM_MenuContext as well as the character contexts, and the character ones
	// go inactive the moment a menu opens, so without this the second press has nowhere to land and
	// the menu cannot be closed with the key that opened it.
	//------------------------------------------------------------------------------------------------
	override void OnMenuUpdate(float tDelta)
	{
		super.OnMenuUpdate(tDelta);

		InputManager input = GetGame().GetInputManager();
		if (input)
			input.ActivateContext("ACM_MenuContext");
	}

	//------------------------------------------------------------------------------------------------
	override void OnMenuClose()
	{
		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().Remove(Refresh);

		m_aHandlers.Clear();

		super.OnMenuClose();
	}

	//------------------------------------------------------------------------------------------------
	//------------------------------------------------------------------------------------------------
	// What the base game calls this group.
	//
	// Custom name if the leader set one, otherwise the callsign assembled the way vanilla assembles
	// it - "Atlas Red 1" - so this panel and the vanilla group menu agree on what the squad is
	// called. The group id is a last resort and only shows if the group has neither.
	//------------------------------------------------------------------------------------------------
	protected string GroupLabel(SCR_AIGroup group)
	{
		if (!group)
			return "NO GROUP";

		string custom = group.GetCustomName();

		// GetCallsigns hands back LOCALISATION KEYS, not text - "#AR-Callsign_US_Company_A" rather
		// than "Atlas". Substituting them straight into the format string is why the header read
		// "Test (#AR-Callsign_US_Company_A #AR-Callsign_US_Platoon_1 1)". Each part is translated
		// first, the same way GetDeviceName translates an item's UI name elsewhere in this mod.
		string company, platoon, squad, character, format;
		group.GetCallsigns(company, platoon, squad, character, format);

		string callsign = format;
		callsign.Replace("%1", WidgetManager.Translate(company));
		callsign.Replace("%2", WidgetManager.Translate(platoon));
		callsign.Replace("%3", WidgetManager.Translate(squad));
		callsign.Replace("%4", WidgetManager.Translate(character));

		// If anything failed to resolve, the callsign is worse than useless - it is noise wrapped
		// around the name the player actually chose. Drop it rather than print a key.
		if (callsign.Contains("#"))
			callsign = "";

		callsign.TrimInPlace();

		if (!custom.IsEmpty() && !callsign.IsEmpty())
			return string.Format("%1 (%2)", custom, callsign);

		if (!custom.IsEmpty())
			return custom;

		if (!callsign.IsEmpty())
			return callsign;

		return string.Format("GROUP %1", group.GetGroupID());
	}

	//------------------------------------------------------------------------------------------------
	protected bool HasMember(int playerId)
	{
		foreach (ACM_SquadMember member : m_aMembers)
		{
			if (member && member.playerId == playerId)
				return true;
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected void BindRoleDrop()
	{
		m_wDropShade = m_wRoot.FindAnyWidget("ACM_SquadRoleDropShade");
		m_wDropPanel = m_wRoot.FindAnyWidget("ACM_SquadRoleDropPanel");
		m_wDropTitle = TextWidget.Cast(m_wRoot.FindAnyWidget("ACM_SquadRoleDropTitle"));
		m_wDropWho = TextWidget.Cast(m_wRoot.FindAnyWidget("ACM_SquadRoleDropWho"));

		// The catcher sits under the panel and over everything else, so a click anywhere outside the
		// list dismisses the picker instead of falling through to the row behind it.
		m_wDropCatch = BindButton("ACM_SquadRoleDropCatch", "role-cancel", 0);
		m_wDropCancel = BindButton("ACM_SquadRoleDropCancel", "role-cancel", 0);

		m_aDropOpt.Clear();
		m_aDropLabel.Clear();

		for (int i = 0; i < ROLE_DROP_ROWS; i++)
		{
			m_aDropOpt.Insert(BindButton("ACM_SquadRoleDropOpt" + i.ToString(), "role-pick", i));
			m_aDropLabel.Insert(TextWidget.Cast(m_wRoot.FindAnyWidget("ACM_SquadRoleDropLabel" + i.ToString())));
		}

		CloseRoleDrop();
	}

	//------------------------------------------------------------------------------------------------
	protected void OpenRoleDrop(int memberIndex)
	{
		if (memberIndex < 0 || memberIndex >= m_aMembers.Count())
			return;

		ACM_SquadMember member = m_aMembers[memberIndex];
		m_iDropPlayerId = member.playerId;

		if (m_wDropWho)
			m_wDropWho.SetText(member.name);

		// Rows come from the CATALOG, not from the enum. The catalog is the list of roles that are
		// actually authored, so a role with no preset behind it cannot appear as a blank line.
		m_aDropRole.Clear();

		array<ref ACM_TacticalRolePreset> presets = ACM_TacticalRoleCatalog.GetAll();
		if (presets)
		{
			foreach (ACM_TacticalRolePreset preset : presets)
			{
				if (!preset)
					continue;

				if (m_aDropRole.Count() >= ROLE_DROP_ROWS)
					break;

				m_aDropRole.Insert(preset.m_eRole);

				int row = m_aDropRole.Count() - 1;
				if (m_aDropLabel[row])
				{
					string label = preset.m_sDisplayName;
					if (label.IsEmpty())
						label = preset.m_sAbbreviation;

					if (preset.m_eRole == member.role)
						label = label + "   [CURRENT]";

					m_aDropLabel[row].SetText(label);
				}
			}
		}

		DrawRoleDrop();
	}

	//------------------------------------------------------------------------------------------------
	// Copies each button's visibility onto its plate.
	//
	// Done in one pass at the end rather than beside every SetVisible call, so a row hidden anywhere
	// in this file cannot leave its box behind - which is exactly the bug an empty green tile is.
	//------------------------------------------------------------------------------------------------
	protected void SyncPlates()
	{
		for (int i = 0; i < m_aPairButton.Count(); i++)
		{
			Widget button = m_aPairButton[i];
			Widget plate = m_aPairPlate[i];

			if (button && plate)
				plate.SetVisible(button.IsVisible());
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void CloseRoleDrop()
	{
		m_iDropPlayerId = -1;
		m_aDropRole.Clear();
		DrawRoleDrop();
	}

	//------------------------------------------------------------------------------------------------
	protected bool RoleDropOpen()
	{
		return m_iDropPlayerId >= 0;
	}

	//------------------------------------------------------------------------------------------------
	protected void DrawRoleDrop()
	{
		bool open = RoleDropOpen();

		if (m_wDropShade)
			m_wDropShade.SetVisible(open);
		if (m_wDropCatch)
			m_wDropCatch.SetVisible(open);
		if (m_wDropPanel)
			m_wDropPanel.SetVisible(open);
		if (m_wDropTitle)
			m_wDropTitle.SetVisible(open);
		if (m_wDropWho)
			m_wDropWho.SetVisible(open);
		if (m_wDropCancel)
			m_wDropCancel.SetVisible(open);

		for (int i = 0; i < m_aDropOpt.Count(); i++)
		{
			bool used = open && i < m_aDropRole.Count();

			if (m_aDropOpt[i])
				m_aDropOpt[i].SetVisible(used);
			if (m_aDropLabel[i])
				m_aDropLabel[i].SetVisible(used);
		}

		SyncPlates();
	}

	//------------------------------------------------------------------------------------------------
	// One palette, used by both the layout and the hover code. The layout ships these same values as
	// literals; they are repeated here because SetColor needs them at runtime and reading a colour
	// back off a widget is not something this codebase does anywhere.
	//------------------------------------------------------------------------------------------------
	protected Color ButtonBase() { return new Color(0.08, 0.20, 0.15, 0.95); }
	protected Color ButtonHover() { return new Color(0.20, 0.46, 0.34, 1.00); }

	//------------------------------------------------------------------------------------------------
	protected Widget BindButton(string name, string action, int index)
	{
		Widget w = m_wRoot.FindAnyWidget(name);
		if (!w)
			return null;

		ACM_SquadClickHandler handler = new ACM_SquadClickHandler(this, action, index);
		m_aHandlers.Insert(handler);
		w.AddHandler(handler);

		// The plate is what gets tinted, never the button - the button is invisible, so recolouring
		// it would change something nobody can see.
		//
		// The picker's click-catcher has no plate on purpose: it is a transparent sheet covering the
		// whole menu, and giving it one would black the screen out.
		Widget plate = m_wRoot.FindAnyWidget(name + "Plate");
		if (plate)
		{
			m_aPairButton.Insert(w);
			m_aPairPlate.Insert(plate);

			if (name.Contains("ACM_SquadRoleDropOpt"))
				handler.SetTint(plate, new Color(0.09, 0.25, 0.18, 0.95), new Color(0.22, 0.50, 0.37, 1.00));
			else if (name == "ACM_SquadRoleDropCancel")
				handler.SetTint(plate, new Color(0.22, 0.10, 0.10, 0.95), new Color(0.44, 0.20, 0.20, 1.00));
			else
				handler.SetTint(plate, new Color(0.12, 0.30, 0.22, 0.97), new Color(0.24, 0.54, 0.40, 1.00));
		}

		return w;
	}

	//------------------------------------------------------------------------------------------------
	// STATE, READ FRESH EVERY REFRESH.
	//
	// Nothing here is cached across ticks. Leadership, membership and roles all change underneath a
	// menu that is left open, and a stale roster is how a leader assigns a role to somebody who has
	// already left the group.
	//------------------------------------------------------------------------------------------------
	protected void Refresh()
	{
		if (!m_wRoot)
			return;

		int localId = SCR_PlayerController.GetLocalPlayerId();
		SCR_AIGroup group = ACM_SquadCommandComponent.ResolveGroup(localId);
		bool isLeader = ACM_SquadCommandComponent.IsLeaderOf(localId, group);

		RebuildMembers(group, localId);

		if (m_wGroupName)
		{
			if (group)
				m_wGroupName.SetText(string.Format("%1   %2 MEMBER(S)", GroupLabel(group), m_aMembers.Count()));
			else
				m_wGroupName.SetText("NO GROUP");
		}

		if (m_wLeaderState)
		{
			if (!group)
				m_wLeaderState.SetText("You are not in a group. Join one to use squad command.");
			else if (isLeader)
				m_wLeaderState.SetText("SQUAD LEADER - you may assign roles, resize, and share the picture.");
			else
				m_wLeaderState.SetText("MEMBER - roster is read only. Only the squad leader may change it.");
		}

		if (m_wSizeValue)
		{
			if (group)
				m_wSizeValue.SetText(group.GetMaxMembers().ToString());
			else
				m_wSizeValue.SetText("-");
		}

		// Resizing is a leader power and needs a group to apply to. Shown only when both hold, so
		// the stepper is never a button that looks live and does nothing.
		bool mayResize = group && isLeader;

		if (m_wSizeMinus)
			m_wSizeMinus.SetVisible(mayResize);
		if (m_wSizeMinusBg)
			m_wSizeMinusBg.SetVisible(mayResize);
		if (m_wSizePlus)
			m_wSizePlus.SetVisible(mayResize);
		if (m_wSizePlusBg)
			m_wSizePlusBg.SetVisible(mayResize);

		// Renaming is a leader power, same as resizing.
		bool mayRename = group && isLeader;

		if (m_wNameEdit)
			m_wNameEdit.SetVisible(mayRename);
		if (m_wNameEditBg)
			m_wNameEditBg.SetVisible(mayRename);
		if (m_wNameSave)
			m_wNameSave.SetVisible(mayRename);

		// Seeded once with the current name and then left alone. Rewriting it every refresh would
		// wipe whatever the player is in the middle of typing.
		if (mayRename && m_wNameEdit && !m_bNameEditDirty)
		{
			m_wNameEdit.SetText(group.GetCustomName());
			m_bNameEditDirty = true;
		}

		if (!group)
			m_bNameEditDirty = false;

		RefreshRoster(isLeader);

		// The roster is rebuilt twice a second. If the player being edited has left the group in
		// the meantime, the picker is editing nobody and is dismissed rather than left open over a
		// name that is no longer there.
		if (RoleDropOpen() && !HasMember(m_iDropPlayerId))
			CloseRoleDrop();
		else
			DrawRoleDrop();

		SyncPlates();
	}

	//------------------------------------------------------------------------------------------------
	protected void RebuildMembers(SCR_AIGroup group, int localId)
	{
		m_aMembers.Clear();

		if (!group)
			return;

		PlayerManager players = GetGame().GetPlayerManager();
		if (!players)
			return;

		int leaderId = group.GetLeaderID();

		array<int> ids = group.GetPlayerIDs();
		if (!ids)
			return;

		foreach (int id : ids)
		{
			if (id <= 0)
				continue;

			string name = ACM_PlayerNames.GetDisplayName(id);
			if (name.IsEmpty())
				name = "PLAYER " + id.ToString();

			name.ToUpper();

			m_aMembers.Insert(new ACM_SquadMember(id, name, ResolveRole(id), id == leaderId, id == localId));
		}
	}

	//------------------------------------------------------------------------------------------------
	protected ACM_ETacticalRole ResolveRole(int playerId)
	{
		ACM_TacticalRoleComponent registry = ACM_TacticalRoleComponent.GetInstance();
		if (!registry)
			return ACM_ETacticalRole.UNASSIGNED;

		return registry.GetTacticalRole(playerId);
	}

	//------------------------------------------------------------------------------------------------
	protected void RefreshRoster(bool isLeader)
	{
		for (int i = 0; i < m_aRosterName.Count(); i++)
		{
			bool used = i < m_aMembers.Count();

			TextWidget nameWidget = m_aRosterName[i];
			TextWidget roleWidget = m_aRosterRole[i];
			Widget button = m_aRosterButton[i];

			if (!used)
			{
				if (nameWidget)
					nameWidget.SetVisible(false);
				if (roleWidget)
					roleWidget.SetVisible(false);
				if (button)
					button.SetVisible(false);
				if (m_aRosterButtonBg[i])
					m_aRosterButtonBg[i].SetVisible(false);
				if (m_aRosterNameBg[i])
					m_aRosterNameBg[i].SetVisible(false);
				if (m_aRosterRoleBg[i])
					m_aRosterRoleBg[i].SetVisible(false);

				continue;
			}

			ACM_SquadMember member = m_aMembers[i];

			if (m_aRosterNameBg[i])
				m_aRosterNameBg[i].SetVisible(true);
			if (m_aRosterRoleBg[i])
				m_aRosterRoleBg[i].SetVisible(true);

			if (nameWidget)
			{
				string label = member.name;
				if (member.isLeader)
					label = label + "  [LEAD]";

				nameWidget.SetText(label);
				nameWidget.SetVisible(true);
			}

			if (roleWidget)
			{
				ACM_TacticalRolePreset preset = ACM_TacticalRoleCatalog.GetPreset(member.role);
				if (preset)
				{
					roleWidget.SetText(preset.m_sDisplayName);

					if (preset.m_Color)
						roleWidget.SetColor(preset.m_Color);
				}

				roleWidget.SetVisible(true);
			}

			// A member may always change their own role - that is the pre-existing behaviour and it
			// is not a leader power. A leader may additionally change anyone else's in their group.
			bool mayEdit = member.isLocal || isLeader;

			if (button)
				button.SetVisible(mayEdit);
			if (m_aRosterButtonBg[i])
				m_aRosterButtonBg[i].SetVisible(mayEdit);
		}
	}

	//------------------------------------------------------------------------------------------------
	// CLICKS - every one of these ASKS and then returns. None of them change state.
	//------------------------------------------------------------------------------------------------
	void OnRowClicked(string action, int index)
	{
		ACM_PlayerLinkComponent link = ACM_PlayerLinkComponent.GetLocal();

		if (action == "close")
		{
			Close();
			return;
		}

		if (!link)
			return;

		int localId = SCR_PlayerController.GetLocalPlayerId();
		SCR_AIGroup group = ACM_SquadCommandComponent.ResolveGroup(localId);
		if (!group)
			return;

		// RESIZE, WITH A FLOOR.
		//
		// Still routed through ACM_PlayerLinkComponent to SCR_AIGroup.SetMaxMembers, because that is
		// what this build of the game actually exposes - the newer SCR_PlayerControllerGroupComponent
		// request methods in the published API docs are not present here, and assuming they were is
		// what broke the last compile.
		//
		// The guard is the part worth keeping. Driving max members below the people already standing
		// in the group, or to zero, leaves it in a shape the vanilla group UI declines to edit.
		if (action == "size-" || action == "size+")
		{
			int size = group.GetMaxMembers();

			if (action == "size-")
				size = size - 1;
			else
				size = size + 1;

			int floor = m_aMembers.Count();
			if (floor < 1)
				floor = 1;

			if (size < floor)
				return;

			link.AskSetSquadSize(size);
			return;
		}

		if (action == "name-save")
		{
			if (!m_wNameEdit)
				return;

			string typed = m_wNameEdit.GetText();
			typed.TrimInPlace();

			if (typed.IsEmpty())
				return;

			// Asks and returns. The name that comes back is whatever the server accepted, read on a
			// later refresh from the group itself - this never writes the label directly.
			link.AskSetGroupName(typed);
			return;
		}

		if (action == "role")
		{
			// Opens the picker instead of stepping to the next role. Cycling meant fifteen clicks to
			// reach the role you wanted and no way to see what the options were.
			OpenRoleDrop(index);
			return;
		}

		if (action == "role-cancel")
		{
			CloseRoleDrop();
			return;
		}

		if (action == "role-pick")
		{
			if (!RoleDropOpen())
				return;

			if (index < 0 || index >= m_aDropRole.Count())
				return;

			int chosen = m_aDropRole[index];
			int target = m_iDropPlayerId;

			// Closed before the ask, not after. The reply arrives through replicated state on a
			// later frame, so leaving the panel up would make it look like nothing happened.
			CloseRoleDrop();

			link.AskSetMemberRole(target, chosen);
			return;
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Next role in the catalog, wrapping. Written out rather than done with arithmetic on the enum
	//! so a role added later cannot be silently skipped or landed on out of order.
	protected int NextRole(ACM_ETacticalRole current)
	{
		// Walks the CATALOG rather than doing arithmetic on the enum. The catalog is the list of
		// roles that actually exist and are authored; incrementing the enum would land on values
		// with no preset behind them and show a blank role.
		array<ref ACM_TacticalRolePreset> presets = ACM_TacticalRoleCatalog.GetAll();
		if (!presets || presets.IsEmpty())
			return ACM_ETacticalRole.UNASSIGNED;

		int at = -1;
		for (int i = 0; i < presets.Count(); i++)
		{
			if (presets[i] && presets[i].m_eRole == current)
			{
				at = i;
				break;
			}
		}

		at++;
		if (at >= presets.Count())
			at = 0;

		if (!presets[at])
			return ACM_ETacticalRole.UNASSIGNED;

		return presets[at].m_eRole;
	}
}
