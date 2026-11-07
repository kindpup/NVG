//------------------------------------------------------------------------------------------------
// ACM_IvasMenuUI.c
// The IVAS control set as a flat list: the same six branches the radial offers, one after another.
//
// Section membership and the hardware gates are copied from ACM_RadialMenuItemsComponent rather
// than reinvented. A control whose hardware is absent is not offered here either - a Data Link
// switch on goggles with no data link is worse than no switch at all.
//------------------------------------------------------------------------------------------------
class ACM_IvasMenuUI : ACM_SystemMenuBase
{
	//------------------------------------------------------------------------------------------------
	override protected string MenuTitle() { return "ACM IVAS"; }

	//------------------------------------------------------------------------------------------------
	override protected string MenuHint()
	{
		return "Everything the IVAS branch of the radial offers, as a list.";
	}

	//------------------------------------------------------------------------------------------------
	override protected void BuildRows()
	{
		// Re-decide authority before building anything, matching the radial. The background poll
		// gets there within a second, but a player whose menu came up without its controls will
		// close it and open it again straight away - so make that reopen the thing that fixes it.
		ACM_TargetDetectionComponent.RefreshAllAuthority();

		ACM_TargetDetectionComponent device = ACM_TargetDetectionComponent.GetLocalDevice();
		if (!device)
		{
			SetSubtitle("No IVAS hardware in your loadout.");
			return;
		}

		SetSubtitle(DeviceName(device.GetOwner()));

		AddSystemRows(device);
		AddIffRows();
		AddTargetingRows();
		AddDataLinkRows();
		AddFeedRows(device);
		AddDisplayRows();
	}

	//------------------------------------------------------------------------------------------------
	// SYSTEM: the hardware itself. Power and the visor.
	//
	// Night vision and thermal are deliberately NOT here - they are separate visual systems with
	// their own power and their own menus, and switching IVAS off must not take them with it.
	//------------------------------------------------------------------------------------------------
	protected void AddSystemRows(notnull ACM_TargetDetectionComponent device)
	{
		AddHeader("SYSTEM");
		AddAction(new ACM_IvasMenuEntry(ACM_EIvasAction.IVAS_MASTER));

		IEntity owner = device.GetOwner();

		// Visor before anything optical: with it stowed there is nothing to show through.
		if (owner && device.HasVisorHardware())
			AddAction(new ACM_IvasVisorEntry(owner));
	}

	//------------------------------------------------------------------------------------------------
	protected void AddIffRows()
	{
		AddHeader("IFF");
		AddAction(new ACM_IvasMenuEntry(ACM_EIvasAction.FRIENDLY_IFF));
		AddAction(new ACM_IvasMenuEntry(ACM_EIvasAction.FRIENDLY_SCOPE));
		AddAction(new ACM_IvasMenuEntry(ACM_EIvasAction.FRIENDLY_DETAIL));
		AddAction(new ACM_IvasMenuEntry(ACM_EIvasAction.FRIENDLY_SILHOUETTES));
		AddAction(new ACM_IvasMenuEntry(ACM_EIvasAction.CIVILIAN_IFF));
		AddAction(new ACM_IvasMenuEntry(ACM_EIvasAction.UNKNOWN_CONTACTS));
	}

	//------------------------------------------------------------------------------------------------
	protected void AddTargetingRows()
	{
		AddHeader("TARGETING");
		AddAction(new ACM_IvasMenuEntry(ACM_EIvasAction.HOSTILE_TRACKING));
		AddAction(new ACM_IvasMenuEntry(ACM_EIvasAction.CONTACT_FILTER));
		AddAction(new ACM_IvasMenuEntry(ACM_EIvasAction.CONTACT_DETAIL));
		AddAction(new ACM_IvasMenuEntry(ACM_EIvasAction.HOSTILE_SILHOUETTES));
		AddAction(new ACM_IvasMenuEntry(ACM_EIvasAction.STALE_CONTACTS));
	}

	//------------------------------------------------------------------------------------------------
	// Only offered where there is a link to control. These toggles change what this client offers
	// and accepts; the server keeps deciding who may report and who may receive.
	//------------------------------------------------------------------------------------------------
	protected void AddDataLinkRows()
	{
		if (!ACM_TacticalContactManager.GetInstance())
			return;

		AddHeader("DATA LINK");
		AddAction(new ACM_IvasMenuEntry(ACM_EIvasAction.DATALINK_MASTER));
		AddAction(new ACM_IvasMenuEntry(ACM_EIvasAction.DATALINK_RECEIVE));
		AddAction(new ACM_IvasMenuEntry(ACM_EIvasAction.DATALINK_SHARE));
		AddAction(new ACM_IvasMenuEntry(ACM_EIvasAction.DATALINK_STATUS));
	}

	//------------------------------------------------------------------------------------------------
	// Hardware test, not an authority test. The entries handle not being in control.
	//------------------------------------------------------------------------------------------------
	protected void AddFeedRows(notnull ACM_TargetDetectionComponent device)
	{
		if (!device.HasVisorHardware())
			return;

		AddHeader("REMOTE FEED");
		AddAction(new ACM_IvasMenuEntry(ACM_EIvasAction.FEED_MASTER));
		AddAction(new ACM_IvasMenuEntry(ACM_EIvasAction.FEED_NEXT));
		AddAction(new ACM_IvasMenuEntry(ACM_EIvasAction.FEED_PREV));
		AddAction(new ACM_IvasMenuEntry(ACM_EIvasAction.FEED_SIZE));
		AddAction(new ACM_IvasMenuEntry(ACM_EIvasAction.FEED_POSITION));
	}

	//------------------------------------------------------------------------------------------------
	protected void AddDisplayRows()
	{
		AddHeader("DISPLAY");
		AddAction(new ACM_IvasMenuEntry(ACM_EIvasAction.HUD));
		AddAction(new ACM_IvasMenuEntry(ACM_EIvasAction.MARKER_SCALE));
		AddAction(new ACM_IvasMenuEntry(ACM_EIvasAction.CONTACT_LABELS));
		AddAction(new ACM_IvasMenuEntry(ACM_EIvasAction.LABEL_DISTANCE));
		AddAction(new ACM_IvasMenuEntry(ACM_EIvasAction.LABEL_NAMES));
	}
}
