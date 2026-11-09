//------------------------------------------------------------------------------------------------
// ACM_SystemMenuBase.c
// Shared chrome for the five per-system menus: Lights, Lasers, NVG, Thermal and IVAS.
//
// All five share ONE layout (UI/layouts/Menus/ACM_SystemMenu.layout) and differ only in the rows
// they build. That is deliberate: five near-identical layout files would drift apart the first time
// anyone nudged a margin, and a menu whose panel is eight pixels off from its neighbour reads as a
// bug even when every control works.
//
//------------------------------------------------------------------------------------------------
// THIS FILE PERFORMS NOTHING ITSELF.
//
// A row either carries an SCR_SelectionMenuEntry built by the subclass, or it is a header and
// carries nothing. Clicking a row calls OnPerform() on that entry - the same call the radial menu
// makes for the same entry object. There is no second implementation of "turn the flashlight on"
// living in the UI, so the radial and these menus cannot disagree about what an action does.
//
// The rule for editing: if you find yourself reaching for a component setter in this file or in a
// subclass, stop and build the matching radial entry instead. Anything the radial cannot already do
// belongs in the entry classes, not here.
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
// One rendered line. Headers are labels only - no entry, no click, drawn dimmer.
//------------------------------------------------------------------------------------------------
class ACM_SysMenuRow
{
	bool m_bHeader;

	// Used verbatim for headers, and as an override for an action whose entry label is not the
	// wording this menu wants. Left empty, an action row asks its entry what it is called.
	string m_sLabel;

	// Appended after the entry's own label - "[CURRENT]" on the phosphor colour in use, and so on.
	string m_sSuffix;

	ref SCR_SelectionMenuEntry m_Entry;

	//------------------------------------------------------------------------------------------------
	void ACM_SysMenuRow(string label, SCR_SelectionMenuEntry entry, bool header, string suffix = "")
	{
		m_sLabel = label;
		m_Entry = entry;
		m_bHeader = header;
		m_sSuffix = suffix;
	}

	//------------------------------------------------------------------------------------------------
	// Resolved fresh on every draw, never cached. These labels report live hardware state, and a
	// cached one is a label that lies the moment anything else changes it.
	//------------------------------------------------------------------------------------------------
	string ResolveText()
	{
		string text = m_sLabel;

		if (!m_bHeader && m_Entry && text.IsEmpty())
			text = m_Entry.ACM_MenuLabel();

		if (!m_sSuffix.IsEmpty())
			text = text + "  " + m_sSuffix;

		return text;
	}
}

//------------------------------------------------------------------------------------------------
// Click routing for a plain widget, matching ACM_SquadClickHandler. Not built on a vanilla button
// prefab for the same reason: those carry layouts and component expectations this menu does not
// need, and all that is wanted here is "tell me which row was clicked".
//------------------------------------------------------------------------------------------------
class ACM_SysMenuClickHandler : ScriptedWidgetEventHandler
{
	protected ACM_SystemMenuBase m_Menu;
	protected int m_iIndex;

	// Hover tint, same approach as the squad menu: repaint the plate rather than carry a second
	// highlight widget that has to be kept in sync with it.
	protected Widget m_wTint;
	protected ref Color m_ColBase;
	protected ref Color m_ColHover;

	//------------------------------------------------------------------------------------------------
	void ACM_SysMenuClickHandler(ACM_SystemMenuBase menu, int index)
	{
		m_Menu = menu;
		m_iIndex = index;
	}

	//------------------------------------------------------------------------------------------------
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
			m_Menu.OnRowClicked(m_iIndex);

		return true;
	}
}

//------------------------------------------------------------------------------------------------
class ACM_SystemMenuBase : ChimeraMenuBase
{
	// Fixed widgets rather than dynamic creation, matching every other panel in this mod: the
	// layout owns the widgets and the script only ever fills or hides them.
	protected static const int ROW_COUNT = 32;

	// The close button is index -1 so it can share the row handler and its click path.
	protected static const int CLOSE_INDEX = -1;

	protected Widget m_wRoot;
	protected TextWidget m_wTitle;
	protected TextWidget m_wSubtitle;
	protected TextWidget m_wHint;
	protected TextWidget m_wStatus;

	// Three widgets per line, not one.
	//
	// m_aRowWidget is a ButtonWidgetClass - a bare ImageWidget receives no pointer events at all,
	// which is exactly why the first version of this menu drew fine and clicked nothing.
	// m_aRowLabel is that button's own caption, nested inside it so the whole tile is the hit
	// target. m_aRowHeader is a separate plate-less line used for section titles, kept OUTSIDE the
	// button because hiding a button hides its children and a header has to survive that.
	protected ref array<Widget> m_aRowWidget = {};
	protected ref array<TextWidget> m_aRowLabel = {};
	protected ref array<TextWidget> m_aRowHeader = {};

	// THE VISIBLE BOX.
	//
	// A ButtonWidgetClass with "style blank" paints nothing at all - its Color is ignored - so the
	// button cannot be the box. It is a transparent hit area laid over this plate, and this plate is
	// what gets shown, hidden and tinted.
	protected ref array<Widget> m_aRowPlate = {};

	// Handlers are kept alive here. A ScriptedWidgetEventHandler attached with SetHandler is not
	// owned by the widget, so dropping the reference collects it and the row silently stops
	// responding - which looks exactly like a broken layout.
	protected ref array<ref ACM_SysMenuClickHandler> m_aHandlers = {};

	// The rows as of the last build. Clicks index into THIS, never into a widget's position, so a
	// loadout that changed between the draw and the click cannot resolve to the wrong device.
	protected ref array<ref ACM_SysMenuRow> m_aRows = {};

	//------------------------------------------------------------------------------------------------
	// Subclass surface. Four overrides and nothing else.
	//------------------------------------------------------------------------------------------------
	protected string MenuTitle() { return "ACM SYSTEM"; }
	protected string MenuHint() { return ""; }
	protected void BuildRows() { }

	//------------------------------------------------------------------------------------------------
	override void OnMenuOpen()
	{
		super.OnMenuOpen();

		m_wRoot = GetRootWidget();
		if (!m_wRoot)
			return;

		m_wTitle = TextWidget.Cast(m_wRoot.FindAnyWidget("ACM_SysMenuTitle"));
		m_wSubtitle = TextWidget.Cast(m_wRoot.FindAnyWidget("ACM_SysMenuSubtitle"));
		m_wHint = TextWidget.Cast(m_wRoot.FindAnyWidget("ACM_SysMenuHint"));
		m_wStatus = TextWidget.Cast(m_wRoot.FindAnyWidget("ACM_SysMenuStatus"));

		if (m_wTitle)
			m_wTitle.SetText(MenuTitle());

		if (m_wHint)
			m_wHint.SetText(MenuHint());

		TextWidget closeLabel = TextWidget.Cast(m_wRoot.FindAnyWidget("ACM_SysMenuCloseLabel"));
		if (closeLabel)
			closeLabel.SetText("CLOSE");

		BindRow("ACM_SysMenuClose", CLOSE_INDEX);

		m_aRowWidget.Clear();
		m_aRowLabel.Clear();
		m_aRowHeader.Clear();
		m_aRowPlate.Clear();

		for (int i = 0; i < ROW_COUNT; i++)
		{
			// Plate first: BindRow tints whatever plate is already recorded for this index.
			m_aRowPlate.Insert(m_wRoot.FindAnyWidget("ACM_SysMenuRowBg" + i.ToString()));

			m_aRowWidget.Insert(BindRow("ACM_SysMenuRow" + i.ToString(), i));
			m_aRowLabel.Insert(TextWidget.Cast(m_wRoot.FindAnyWidget("ACM_SysMenuLabel" + i.ToString())));
			m_aRowHeader.Insert(TextWidget.Cast(m_wRoot.FindAnyWidget("ACM_SysMenuHdr" + i.ToString())));
		}

		Rebuild();
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
		m_aHandlers.Clear();
		m_aRows.Clear();

		super.OnMenuClose();
	}

	//------------------------------------------------------------------------------------------------
	protected Widget BindRow(string widgetName, int index)
	{
		Widget w = m_wRoot.FindAnyWidget(widgetName);
		if (!w)
			return null;

		ACM_SysMenuClickHandler handler = new ACM_SysMenuClickHandler(this, index);
		m_aHandlers.Insert(handler);
		w.AddHandler(handler);

		// Tint the PLATE. The button itself is transparent and paints nothing, so tinting it would
		// change a colour nobody can see.
		//
		// Base colours mirror the literals the layout ships. Reading a colour back off a widget is
		// not something this codebase does anywhere, so they are restated rather than queried.
		if (index == CLOSE_INDEX)
		{
			Widget closePlate = m_wRoot.FindAnyWidget("ACM_SysMenuCloseBg");
			if (closePlate)
				handler.SetTint(closePlate, new Color(0.13, 0.32, 0.24, 0.98), new Color(0.24, 0.54, 0.40, 1.00));
		}
		else if (index >= 0 && index < m_aRowPlate.Count())
		{
			Widget plate = m_aRowPlate.Get(index);
			if (plate)
				handler.SetTint(plate, new Color(0.09, 0.25, 0.18, 0.95), new Color(0.22, 0.50, 0.37, 1.00));
		}

		return w;
	}

	//------------------------------------------------------------------------------------------------
	// Rebuild rather than refresh. Every label in these menus reports live hardware state, and a
	// device can leave the loadout while the menu is open, so recomputing the whole list is both
	// simpler and more correct than trying to patch individual rows.
	//------------------------------------------------------------------------------------------------
	protected void Rebuild()
	{
		m_aRows.Clear();
		BuildRows();
		Draw();
	}

	//------------------------------------------------------------------------------------------------
	protected void Draw()
	{
		int shown = m_aRows.Count();
		if (shown > ROW_COUNT)
			shown = ROW_COUNT;

		for (int i = 0; i < ROW_COUNT; i++)
		{
			Widget row = m_aRowWidget.Get(i);
			Widget plate = m_aRowPlate.Get(i);
			TextWidget header = m_aRowHeader.Get(i);

			if (i >= shown)
			{
				// Hiding the button hides its nested caption with it; the plate is a sibling and
				// has to be hidden in its own right or it stays as an empty box.
				if (row)
					row.SetVisible(false);
				if (plate)
					plate.SetVisible(false);
				if (header)
					header.SetVisible(false);

				continue;
			}

			ACM_SysMenuRow model = m_aRows.Get(i);
			string text = model.ResolveText();

			// A line is either a header or a control, never both. Whichever it is not gets hidden
			// outright rather than left blank - a visible empty plate reads as a broken button.
			if (model.m_bHeader)
			{
				if (row)
					row.SetVisible(false);
				if (plate)
					plate.SetVisible(false);

				if (header)
				{
					header.SetVisible(true);
					header.SetText(text);
				}

				continue;
			}

			if (header)
				header.SetVisible(false);

			if (row)
				row.SetVisible(true);
			if (plate)
				plate.SetVisible(true);

			TextWidget label = m_aRowLabel.Get(i);
			if (label)
			{
				label.SetVisible(true);
				label.SetText(text);
			}
		}

		if (m_wStatus)
		{
			if (m_aRows.Count() > ROW_COUNT)
				m_wStatus.SetText(string.Format("%1 of %2 entries shown - the rest are on the radial menu.", ROW_COUNT, m_aRows.Count()));
			else
				m_wStatus.SetText("");
		}
	}

	//------------------------------------------------------------------------------------------------
	void OnRowClicked(int index)
	{
		if (index == CLOSE_INDEX)
		{
			Close();
			return;
		}

		if (index < 0 || index >= m_aRows.Count())
			return;

		ACM_SysMenuRow model = m_aRows.Get(index);
		if (!model || model.m_bHeader || !model.m_Entry)
			return;

		// Routed through ACM_PerformFromMenu because vanilla declares OnPerform protected. See
		// ACM_SelectionMenuEntryAccess.c - it is one public call onto the same protected method.
		model.m_Entry.ACM_PerformFromMenu();

		// State has moved. Rebuild so every label on screen agrees with the hardware again, not
		// only the one that was clicked - toggling a master switch changes what its dependents say.
		Rebuild();
	}

	//------------------------------------------------------------------------------------------------
	// Row helpers for subclasses.
	//------------------------------------------------------------------------------------------------
	protected void AddHeader(string label)
	{
		// Marked with rules rather than recoloured. Nothing in this mod sets a widget colour from
		// script, and the draw path is the wrong place to be the first thing that tries.
		m_aRows.Insert(new ACM_SysMenuRow("-- " + label + " --", null, true));
	}

	//------------------------------------------------------------------------------------------------
	// Label left empty means "ask the entry" - which is what every row should do unless this menu
	// deliberately wants different wording from the radial.
	protected void AddAction(SCR_SelectionMenuEntry entry, string suffix = "", string labelOverride = "")
	{
		if (!entry)
			return;

		m_aRows.Insert(new ACM_SysMenuRow(labelOverride, entry, false, suffix));
	}

	//------------------------------------------------------------------------------------------------
	protected void SetSubtitle(string text)
	{
		if (m_wSubtitle)
			m_wSubtitle.SetText(text);
	}

	//------------------------------------------------------------------------------------------------
	// Shared device discovery. Same sources and same order as ACM_RadialMenuItemsComponent, so a
	// device that appears on the radial appears here and vice versa.
	//------------------------------------------------------------------------------------------------
	protected ChimeraCharacter LocalCharacter()
	{
		return ChimeraCharacter.Cast(SCR_PlayerController.GetLocalControlledEntity());
	}

	//------------------------------------------------------------------------------------------------
	protected void CollectWeaponAttachments(notnull ChimeraCharacter character, notnull array<IEntity> result)
	{
		BaseWeaponComponent currentWeapon = SCR_WeaponLib.GetCurrentWeaponComponent(character);
		if (!currentWeapon)
			return;

		array<AttachmentSlotComponent> attachments = {};
		currentWeapon.GetAttachments(attachments);

		foreach (AttachmentSlotComponent attachment : attachments)
		{
			IEntity entity = attachment.GetAttachedEntity();
			if (entity)
				CollectFrom(entity, result);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void CollectEquipment(notnull ChimeraCharacter character, notnull array<IEntity> result)
	{
		EquipedLoadoutStorageComponent loadout = EquipedLoadoutStorageComponent.Cast(
			character.FindComponent(EquipedLoadoutStorageComponent));

		if (!loadout)
			return;

		IEntity helmet = loadout.GetClothFromArea(LoadoutHeadCoverArea);
		if (helmet)
			CollectFrom(helmet, result);

		array<IEntity> allEquipped = {};
		loadout.GetAll(allEquipped);

		foreach (IEntity item : allEquipped)
		{
			if (item)
				CollectFrom(item, result);
		}
	}

	//------------------------------------------------------------------------------------------------
	// Walks children AND inventory storage. The storage hop is what reaches a device sitting in an
	// NVG mount's pocket storage - a slotted item's scene parent is a bone, not its container, so a
	// children-only walk misses every device that is mounted rather than worn directly.
	//------------------------------------------------------------------------------------------------
	protected void CollectFrom(IEntity entity, notnull array<IEntity> result)
	{
		if (!entity)
			return;

		if (result.Contains(entity))
			return;

		if (HasAnyACMComponent(entity))
			result.Insert(entity);

		IEntity child = entity.GetChildren();
		while (child)
		{
			CollectFrom(child, result);
			child = child.GetSibling();
		}

		BaseInventoryStorageComponent storage = BaseInventoryStorageComponent.Cast(
			entity.FindComponent(BaseInventoryStorageComponent));

		if (storage)
		{
			array<IEntity> items = {};
			storage.GetAll(items);

			foreach (IEntity item : items)
			{
				if (item)
					CollectFrom(item, result);
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	protected bool HasAnyACMComponent(IEntity entity)
	{
		if (!entity)
			return false;

		if (ACM_NVL_LightsComponent.Cast(entity.FindComponent(ACM_NVL_LightsComponent)))
			return true;

		if (ACM_NVL_StrobeComponent.Cast(entity.FindComponent(ACM_NVL_StrobeComponent)))
			return true;

		if (ACM_NightVisionComponent.Cast(entity.FindComponent(ACM_NightVisionComponent)))
			return true;

		if (ACM_ThermalComponentV2.Cast(entity.FindComponent(ACM_ThermalComponentV2)))
			return true;

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected string DeviceName(IEntity entity)
	{
		if (!entity)
			return "Device";

		InventoryItemComponent itemComp = InventoryItemComponent.Cast(
			entity.FindComponent(InventoryItemComponent));

		if (itemComp)
		{
			UIInfo info = itemComp.GetUIInfo();
			if (info)
			{
				string name = WidgetManager.Translate(info.GetName());
				if (!name.IsEmpty())
					return name;
			}
		}

		return "Device";
	}

	//------------------------------------------------------------------------------------------------
	// Every ACM device the local player is carrying, weapon attachments first.
	//------------------------------------------------------------------------------------------------
	protected void CollectAllDevices(notnull array<IEntity> result)
	{
		ChimeraCharacter character = LocalCharacter();
		if (!character)
			return;

		CollectWeaponAttachments(character, result);
		CollectEquipment(character, result);
	}

	//------------------------------------------------------------------------------------------------
	protected string OnOff(bool state)
	{
		if (state)
			return "ON";

		return "OFF";
	}
}
