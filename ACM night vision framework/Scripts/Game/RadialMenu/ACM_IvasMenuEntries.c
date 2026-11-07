//------------------------------------------------------------------------------------------------
// ACM_IvasMenuEntries.c
// Every player-facing ACM IVAS control, as radial entries.
//
// THE MENU OWNS NOTHING. Each entry reads a getter to draw its own label and calls a setter when
// selected. There is no cached value anywhere in this file, which is why opening and closing the
// radial cannot lose a setting and why the same controls stay reachable from a config file, an
// admin override or a loadout preset without any of them going through here.
//
// CONTROLLER FIRST. Two rules follow from that and both are visible in the code below. Enumerated
// settings CYCLE on select rather than opening another ring - three values behind a sub-ring is
// two extra stick movements for a choice a single press can make. And every stateful entry reads
// its current value in brackets, so a player can see what everything is set to by moving across
// the ring rather than by opening each one.
//
// One Enfusion constraint worth naming, because it shapes the whole design: performing an entry
// closes the radial. There is no way to advance a value and stay open, so cycling a three-value
// setting to the far end costs three opens. That is why each cycle shows a hint with the new
// value - the player gets the feedback without having to reopen to read it.
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
// One action per player-facing control.
//
// A single parameterised entry rather than thirty classes, matching how ACM_DynamicMenuEntry
// already handles the light and laser controls. Thirty near-identical classes would be thirty
// copies of the double-fire guard and the bracket formatting to keep in agreement.
//------------------------------------------------------------------------------------------------
enum ACM_EIvasAction
{
	IVAS_MASTER,
	HUD,

	FRIENDLY_IFF,
	FRIENDLY_SCOPE,
	FRIENDLY_DETAIL,
	FRIENDLY_SILHOUETTES,
	CIVILIAN_IFF,
	UNKNOWN_CONTACTS,

	HOSTILE_TRACKING,
	CONTACT_FILTER,
	CONTACT_DETAIL,
	HOSTILE_SILHOUETTES,
	STALE_CONTACTS,

	DATALINK_MASTER,
	DATALINK_RECEIVE,
	DATALINK_SHARE,
	DATALINK_STATUS,

	FEED_MASTER,
	FEED_NEXT,
	FEED_PREV,
	FEED_SIZE,
	FEED_POSITION,

	MARKER_SCALE,
	CONTACT_LABELS,
	LABEL_DISTANCE,
	LABEL_NAMES,

	RESET_CONFIRM,
	RESET_CANCEL
}

//------------------------------------------------------------------------------------------------
class ACM_IvasMenuEntry : SCR_SelectionMenuEntry
{
	protected ACM_EIvasAction m_eAction;
	protected bool m_bPerformed;

	//------------------------------------------------------------------------------------------------
	void ACM_IvasMenuEntry(ACM_EIvasAction action)
	{
		m_eAction = action;
		SetIcon(ACM_RADIAL_IMAGESET, IconName());
		Update();
	}

	//------------------------------------------------------------------------------------------------
	// The device this player is operating. Resolved fresh on every read rather than held, because
	// the wearer can swap helmets, be possessed by a Game Master, or die between the menu being
	// built and an entry being performed.
	//------------------------------------------------------------------------------------------------
	protected ACM_TargetDetectionComponent Device()
	{
		return ACM_TargetDetectionComponent.GetLocalDevice();
	}

	//------------------------------------------------------------------------------------------------
	protected string IconName()
	{
		switch (m_eAction)
		{
			case ACM_EIvasAction.IVAS_MASTER: return "nvg";
			case ACM_EIvasAction.HUD: return "nvg";
			case ACM_EIvasAction.FEED_MASTER: return "nvg";
			case ACM_EIvasAction.FEED_NEXT: return "category";
			case ACM_EIvasAction.FEED_PREV: return "category";
		}

		return "category";
	}

	//------------------------------------------------------------------------------------------------
	protected string BaseLabel()
	{
		switch (m_eAction)
		{
			case ACM_EIvasAction.IVAS_MASTER: return "IVAS";
			case ACM_EIvasAction.HUD: return "HUD";

			case ACM_EIvasAction.FRIENDLY_IFF: return "Friendly IFF";
			case ACM_EIvasAction.FRIENDLY_SCOPE: return "Friendly Scope";
			case ACM_EIvasAction.FRIENDLY_DETAIL: return "Friendly Detail";
			case ACM_EIvasAction.FRIENDLY_SILHOUETTES: return "Friendly Silhouettes";
			case ACM_EIvasAction.CIVILIAN_IFF: return "Civilian IFF";
			case ACM_EIvasAction.UNKNOWN_CONTACTS: return "Unknown Contacts";

			case ACM_EIvasAction.HOSTILE_TRACKING: return "Hostile Tracking";
			case ACM_EIvasAction.CONTACT_FILTER: return "Contact Filter";
			case ACM_EIvasAction.CONTACT_DETAIL: return "Contact Detail";
			case ACM_EIvasAction.HOSTILE_SILHOUETTES: return "Hostile Silhouettes";
			case ACM_EIvasAction.STALE_CONTACTS: return "Stale Contacts";

			case ACM_EIvasAction.DATALINK_MASTER: return "Data Link";
			case ACM_EIvasAction.DATALINK_RECEIVE: return "Receive Contacts";
			case ACM_EIvasAction.DATALINK_SHARE: return "Share Contacts";
			case ACM_EIvasAction.DATALINK_STATUS: return "Link Status";

			case ACM_EIvasAction.FEED_MASTER: return "Remote Feed";
			case ACM_EIvasAction.FEED_NEXT: return "Next Source";
			case ACM_EIvasAction.FEED_PREV: return "Previous Source";
			case ACM_EIvasAction.FEED_SIZE: return "Feed Size";
			case ACM_EIvasAction.FEED_POSITION: return "Feed Position";

			case ACM_EIvasAction.MARKER_SCALE: return "Marker Scale";
			case ACM_EIvasAction.CONTACT_LABELS: return "Contact Labels";
			case ACM_EIvasAction.LABEL_DISTANCE: return "Label Distance";
			case ACM_EIvasAction.LABEL_NAMES: return "Label Names";

			case ACM_EIvasAction.RESET_CONFIRM: return "Confirm Reset";
			case ACM_EIvasAction.RESET_CANCEL: return "Cancel";
		}

		return "IVAS";
	}

	//------------------------------------------------------------------------------------------------
	// The bracketed current value. Empty means the entry has no state to report - a plain action
	// like Next Source, or the reset confirmation.
	//------------------------------------------------------------------------------------------------
	// Shared by display and action handling so a restricted toggle cannot appear to succeed.
	protected bool ServerAllowsAction()
	{
		int feature;
		switch (m_eAction)
		{
			case ACM_EIvasAction.FRIENDLY_IFF:
			case ACM_EIvasAction.FRIENDLY_SCOPE:
			case ACM_EIvasAction.FRIENDLY_DETAIL:
				feature = ACM_IffServerSettings.FRIENDLY; break;
			case ACM_EIvasAction.FRIENDLY_SILHOUETTES:
				feature = ACM_IffServerSettings.FRIENDLY | ACM_IffServerSettings.FRIENDLY_OUTLINE; break;
			case ACM_EIvasAction.CIVILIAN_IFF:
				feature = ACM_IffServerSettings.CIVILIAN; break;
			case ACM_EIvasAction.UNKNOWN_CONTACTS:
				feature = ACM_IffServerSettings.UNKNOWN; break;
			case ACM_EIvasAction.HOSTILE_TRACKING:
			case ACM_EIvasAction.STALE_CONTACTS:
				feature = ACM_IffServerSettings.HOSTILE; break;
			case ACM_EIvasAction.HOSTILE_SILHOUETTES:
				feature = ACM_IffServerSettings.HOSTILE | ACM_IffServerSettings.HOSTILE_OUTLINE; break;
			case ACM_EIvasAction.DATALINK_MASTER:
			case ACM_EIvasAction.DATALINK_RECEIVE:
			case ACM_EIvasAction.DATALINK_SHARE:
				feature = ACM_IffServerSettings.HOSTILE | ACM_IffServerSettings.DATA_LINK; break;
			default: return true;
		}
		return ACM_IffServerSettings.Allows(feature);
	}

	protected string StateText()
	{
		if (!ServerAllowsAction())
			return "SERVER DISABLED";

		ACM_TargetDetectionComponent device = Device();
		if (!device)
			return string.Empty;

		switch (m_eAction)
		{
			case ACM_EIvasAction.IVAS_MASTER: return OnOff(device.IsIvasEnabled());
			case ACM_EIvasAction.HUD: return OnOff(device.IsHudEnabled());

			case ACM_EIvasAction.FRIENDLY_IFF: return OnOff(device.IsFriendlyIffEnabled());
			case ACM_EIvasAction.FRIENDLY_SCOPE: return ScopeName(device.GetFriendlyIffScope());
			case ACM_EIvasAction.FRIENDLY_DETAIL: return DetailName(device.GetIffDetail());
			case ACM_EIvasAction.FRIENDLY_SILHOUETTES: return OnOff(device.OutlinesFriendlies());
			case ACM_EIvasAction.CIVILIAN_IFF: return OnOff(device.IsCivilianIffEnabled());
			case ACM_EIvasAction.UNKNOWN_CONTACTS: return OnOff(device.ShowsUnknownContacts());

			case ACM_EIvasAction.HOSTILE_TRACKING: return OnOff(device.IsHostileTrackingEnabled());
			case ACM_EIvasAction.CONTACT_FILTER: return FilterName(device.GetContactFilter());
			case ACM_EIvasAction.CONTACT_DETAIL: return ContactDetailName(device.GetContactDetail());
			case ACM_EIvasAction.HOSTILE_SILHOUETTES: return OnOff(device.OutlinesConfirmedContacts());
			case ACM_EIvasAction.STALE_CONTACTS: return ShowHide(device.ShowsStaleContacts());

			case ACM_EIvasAction.DATALINK_MASTER: return OnOff(device.IsDataLinkEnabled());
			case ACM_EIvasAction.DATALINK_RECEIVE: return SuspendableOnOff(device.IsReceiveIntent(), device.IsDataLinkEnabled());
			case ACM_EIvasAction.DATALINK_SHARE: return SuspendableOnOff(device.IsShareIntent(), device.IsDataLinkEnabled());
			case ACM_EIvasAction.DATALINK_STATUS: return LinkStatusText();

			case ACM_EIvasAction.FEED_MASTER: return OnOff(ACM_RemoteFeedRenderer.GetInstance().IsActive());
			case ACM_EIvasAction.FEED_NEXT: return ActiveSourceName();
			case ACM_EIvasAction.FEED_SIZE: return FeedSizeName(device.GetFeedSizePreset());
			case ACM_EIvasAction.FEED_POSITION: return FeedPositionName(device.GetFeedPositionPreset());

			case ACM_EIvasAction.MARKER_SCALE: return ScaleName(device.GetMarkerScalePreset());
			case ACM_EIvasAction.CONTACT_LABELS: return OnOff(device.DrawsLabel());
			case ACM_EIvasAction.LABEL_DISTANCE: return OnOff(device.LabelShowsDistance());
			case ACM_EIvasAction.LABEL_NAMES: return OnOff(device.LabelShowsName());
		}

		return string.Empty;
	}

	//------------------------------------------------------------------------------------------------
	protected string OnOff(bool value)
	{
		if (value)
			return "ON";

		return "OFF";
	}

	//------------------------------------------------------------------------------------------------
	protected string ShowHide(bool value)
	{
		if (value)
			return "SHOW";

		return "HIDE";
	}

	//------------------------------------------------------------------------------------------------
	// A sub-setting the master gate is currently holding down.
	//
	// It reads its own value with a note rather than reading OFF, because those are two different
	// facts and the difference is the whole point of the master switch: the wearer's preference is
	// still ON, the link is simply suspended, and it comes back exactly as it was.
	//------------------------------------------------------------------------------------------------
	protected string SuspendableOnOff(bool intent, bool masterEnabled)
	{
		if (!masterEnabled)
		{
			if (intent)
				return "ON - LINK OFF";

			return "OFF - LINK OFF";
		}

		return OnOff(intent);
	}

	//------------------------------------------------------------------------------------------------
	protected string ScopeName(ACM_TDFriendlyIffMode mode)
	{
		if (mode == ACM_TDFriendlyIffMode.GROUP_LEADER)
			return "LEADER";

		if (mode == ACM_TDFriendlyIffMode.GROUP)
			return "GROUP";

		if (mode == ACM_TDFriendlyIffMode.ALL_FRIENDLIES)
			return "ALL";

		return "OFF";
	}

	//------------------------------------------------------------------------------------------------
	protected string DetailName(ACM_TDIffDetail detail)
	{
		if (detail == ACM_TDIffDetail.MINIMAL)
			return "MINIMAL";

		if (detail == ACM_TDIffDetail.DETAILED)
			return "DETAILED";

		return "STANDARD";
	}

	//------------------------------------------------------------------------------------------------
	protected string ContactDetailName(ACM_TDContactDetail detail)
	{
		if (detail == ACM_TDContactDetail.MINIMAL)
			return "MINIMAL";

		if (detail == ACM_TDContactDetail.DETAILED)
			return "DETAILED";

		return "STANDARD";
	}

	//------------------------------------------------------------------------------------------------
	protected string FilterName(ACM_TDContactFilter filter)
	{
		if (filter == ACM_TDContactFilter.ALL)
			return "ALL";

		if (filter == ACM_TDContactFilter.THREATS)
			return "THREATS";

		return "RELEVANT";
	}

	//------------------------------------------------------------------------------------------------
	protected string ScaleName(ACM_TDMarkerScalePreset preset)
	{
		if (preset == ACM_TDMarkerScalePreset.SMALL)
			return "SMALL";

		if (preset == ACM_TDMarkerScalePreset.LARGE)
			return "LARGE";

		return "NORMAL";
	}

	//------------------------------------------------------------------------------------------------
	protected string FeedSizeName(ACM_TDFeedSizePreset preset)
	{
		if (preset == ACM_TDFeedSizePreset.SMALL)
			return "SMALL";

		if (preset == ACM_TDFeedSizePreset.LARGE)
			return "LARGE";

		return "MEDIUM";
	}

	//------------------------------------------------------------------------------------------------
	protected string FeedPositionName(ACM_TDFeedPositionPreset preset)
	{
		if (preset == ACM_TDFeedPositionPreset.TOP_RIGHT)
			return "TOP RIGHT";

		if (preset == ACM_TDFeedPositionPreset.BOTTOM_LEFT)
			return "BOTTOM LEFT";

		if (preset == ACM_TDFeedPositionPreset.BOTTOM_RIGHT)
			return "BOTTOM RIGHT";

		return "TOP LEFT";
	}

	//------------------------------------------------------------------------------------------------
	// Reported from the contact store rather than from what is on screen, so a display budget can
	// never change this number. That is the readout's whole job: what the device knows.
	//------------------------------------------------------------------------------------------------
	protected string LinkStatusText()
	{
		ACM_TargetDetectionSystem system = ACM_TargetDetectionSystem.GetInstance();
		if (!system)
			return "NO SYSTEM";

		int total = system.GetContactCount();
		int shared = system.GetLinkContactCount();

		if (total == 0)
			return "NO CONTACTS";

		return string.Format("%1 CONTACTS, %2 SHARED", total, shared);
	}

	//------------------------------------------------------------------------------------------------
	protected string ActiveSourceName()
	{
		ACM_RemoteFeedSourceComponent source = ACM_RemoteFeedRenderer.GetInstance().GetSource();
		if (!source)
			return "NO SOURCE";

		return source.GetCallsign();
	}

	//------------------------------------------------------------------------------------------------
	override void Update()
	{
		string state = StateText();

		if (state.IsEmpty())
		{
			SetName(BaseLabel());
			return;
		}

		SetName(string.Format("%1 [%2]", BaseLabel(), state));
	}

	//------------------------------------------------------------------------------------------------
	override void SetEntryComponent(SCR_SelectionMenuEntryComponent entryComponent)
	{
		m_EntryComponent = entryComponent;

		if (entryComponent)
		{
			Widget root = entryComponent.GetRootWidget();
			if (root)
			{
				ImageWidget iconWidget = ImageWidget.Cast(root.FindAnyWidget("Icon"));
				if (iconWidget)
					iconWidget.LoadImageFromSet(0, ACM_RADIAL_IMAGESET, IconName());
			}
		}

		Update();
	}

	//------------------------------------------------------------------------------------------------
	override void OnPerform()
	{
		// The same double-fire guard the rest of this menu uses. A player carrying two devices
		// otherwise gets two performs per press, which cancels out on anything that toggles.
		if (m_bPerformed)
			return;

		m_bPerformed = true;

		ACM_TargetDetectionComponent device = Device();
		if (!device)
			return;

		Apply(device);
	}

	//------------------------------------------------------------------------------------------------
	// Every write in this file, in one place.
	//
	// All of it local. Nothing here writes an MP signal, sends an RPC, changes squad scope or
	// touches anything the server owns - the data link entries change what this client OFFERS and
	// ACCEPTS, and the server goes on deciding what it is allowed to do with either. A menu toggle
	// cannot widen the player's authorised network.
	//------------------------------------------------------------------------------------------------
	protected void Apply(notnull ACM_TargetDetectionComponent device)
	{
		if (!ServerAllowsAction())
		{
			Announce("IFF", "Disabled by server");
			return;
		}

		switch (m_eAction)
		{
			case ACM_EIvasAction.IVAS_MASTER:
				device.SetIvasEnabled(!device.IsIvasEnabled());
				Announce("IVAS", OnOff(device.IsIvasEnabled()));
				break;

			case ACM_EIvasAction.HUD:
				device.SetHudEnabled(!device.IsHudEnabled());
				Announce("HUD", OnOff(device.IsHudEnabled()));
				break;

			case ACM_EIvasAction.FRIENDLY_IFF:
				device.SetFriendlyIffEnabled(!device.IsFriendlyIffEnabled());
				Announce("Friendly IFF", OnOff(device.IsFriendlyIffEnabled()));
				break;

			case ACM_EIvasAction.FRIENDLY_SCOPE:
				device.SetFriendlyIffMode(NextScope(device.GetFriendlyIffScope()));
				Announce("Friendly Scope", ScopeName(device.GetFriendlyIffScope()));
				break;

			case ACM_EIvasAction.FRIENDLY_DETAIL:
				device.SetIffDetail(NextIffDetail(device.GetIffDetail()));
				Announce("Friendly Detail", DetailName(device.GetIffDetail()));
				break;

			case ACM_EIvasAction.FRIENDLY_SILHOUETTES:
				device.SetOutlinesFriendlies(!device.OutlinesFriendlies());
				Announce("Friendly Silhouettes", OnOff(device.OutlinesFriendlies()));
				break;

			case ACM_EIvasAction.CIVILIAN_IFF:
				device.SetCivilianIffEnabled(!device.IsCivilianIffEnabled());
				Announce("Civilian IFF", OnOff(device.IsCivilianIffEnabled()));
				break;

			case ACM_EIvasAction.UNKNOWN_CONTACTS:
				device.SetShowUnknownContacts(!device.ShowsUnknownContacts());
				Announce("Unknown Contacts", OnOff(device.ShowsUnknownContacts()));
				break;

			case ACM_EIvasAction.HOSTILE_TRACKING:
				device.SetHostileTrackingEnabled(!device.IsHostileTrackingEnabled());
				Announce("Hostile Tracking", OnOff(device.IsHostileTrackingEnabled()));
				break;

			case ACM_EIvasAction.CONTACT_FILTER:
				device.SetContactFilter(NextFilter(device.GetContactFilter()));
				Announce("Contact Filter", FilterName(device.GetContactFilter()));
				break;

			case ACM_EIvasAction.CONTACT_DETAIL:
				device.SetContactDetail(NextContactDetail(device.GetContactDetail()));
				Announce("Contact Detail", ContactDetailName(device.GetContactDetail()));
				break;

			case ACM_EIvasAction.HOSTILE_SILHOUETTES:
				device.SetOutlinesConfirmed(!device.OutlinesConfirmedContacts());
				Announce("Hostile Silhouettes", OnOff(device.OutlinesConfirmedContacts()));
				break;

			case ACM_EIvasAction.STALE_CONTACTS:
				device.SetShowStaleContacts(!device.ShowsStaleContacts());
				Announce("Stale Contacts", ShowHide(device.ShowsStaleContacts()));
				break;

			case ACM_EIvasAction.DATALINK_MASTER:
				// Parent gate only. Deliberately does not write the two intents underneath it, so
				// switching the link off and back on is a no-op rather than a reset.
				device.SetDataLinkEnabled(!device.IsDataLinkEnabled());
				Announce("Data Link", OnOff(device.IsDataLinkEnabled()));
				break;

			case ACM_EIvasAction.DATALINK_RECEIVE:
				device.SetReceiveIntent(!device.IsReceiveIntent());
				Announce("Receive Contacts", SuspendableOnOff(device.IsReceiveIntent(), device.IsDataLinkEnabled()));
				break;

			case ACM_EIvasAction.DATALINK_SHARE:
				device.SetShareIntent(!device.IsShareIntent());
				Announce("Share Contacts", SuspendableOnOff(device.IsShareIntent(), device.IsDataLinkEnabled()));
				break;

			case ACM_EIvasAction.DATALINK_STATUS:
				// Read-only. Selecting it does nothing on purpose - it is a readout that happens to
				// live on a ring.
				break;

			case ACM_EIvasAction.FEED_MASTER:
				ToggleFeed(device);
				break;

			case ACM_EIvasAction.FEED_NEXT:
				ACM_RemoteFeedRenderer.GetInstance().CycleSource();
				Announce("Remote Feed", ActiveSourceName());
				break;

			case ACM_EIvasAction.FEED_PREV:
				ACM_RemoteFeedRenderer.GetInstance().CyclePrevSource();
				Announce("Remote Feed", ActiveSourceName());
				break;

			case ACM_EIvasAction.FEED_SIZE:
				device.SetFeedSizePreset(NextFeedSize(device.GetFeedSizePreset()));
				Announce("Feed Size", FeedSizeName(device.GetFeedSizePreset()));
				break;

			case ACM_EIvasAction.FEED_POSITION:
				device.SetFeedPositionPreset(NextFeedPosition(device.GetFeedPositionPreset()));
				Announce("Feed Position", FeedPositionName(device.GetFeedPositionPreset()));
				break;

			case ACM_EIvasAction.MARKER_SCALE:
				device.SetMarkerScalePreset(NextScale(device.GetMarkerScalePreset()));
				Announce("Marker Scale", ScaleName(device.GetMarkerScalePreset()));
				break;

			case ACM_EIvasAction.CONTACT_LABELS:
				device.SetDrawsLabel(!device.DrawsLabel());
				Announce("Contact Labels", OnOff(device.DrawsLabel()));
				break;

			case ACM_EIvasAction.LABEL_DISTANCE:
				device.SetLabelShowsDistance(!device.LabelShowsDistance());
				Announce("Label Distance", OnOff(device.LabelShowsDistance()));
				break;

			case ACM_EIvasAction.LABEL_NAMES:
				device.SetLabelShowsName(!device.LabelShowsName());
				Announce("Label Names", OnOff(device.LabelShowsName()));
				break;

			case ACM_EIvasAction.RESET_CONFIRM:
				// Scope is this device's own presentation preferences. It does not touch the
				// tactical role registry, group membership, data link authorisation or anything
				// else the server owns.
				device.ResetIvasSettings();
				Announce("IVAS Settings", "RESET TO DEFAULTS");
				break;

			case ACM_EIvasAction.RESET_CANCEL:
				// Nothing, which is the entire feature. It exists so the confirmation ring has a
				// way out that does not depend on the player finding a back button on a gamepad.
				break;
		}
	}

	//------------------------------------------------------------------------------------------------
	// The feed follows the device's own rules: it lives inside this display and is powered by the
	// visor, so the region is pushed before anything is asked to render.
	//------------------------------------------------------------------------------------------------
	protected void ToggleFeed(notnull ACM_TargetDetectionComponent device)
	{
		ACM_RemoteFeedRenderer renderer = ACM_RemoteFeedRenderer.GetInstance();

		if (renderer.IsActive())
		{
			// Faded, not hard-stopped. This is the wearer switching the feed off, which is the one
			// case worth an animation - every other caller of Stop() is a teardown that needs the
			// camera slot back immediately.
			renderer.StopFaded();
			Announce("Remote Feed", "OFF");
			return;
		}

		// Authority matters here in a way it does not for a presentation setting. The feed drives a
		// shared camera slot on this machine, and the visor that powers it is a replicated signal.
		if (!device.HasLocalAuthority())
		{
			Announce("Remote Feed", "NO CONTROL OF THIS DEVICE");
			return;
		}

		if (device.HasVisorHardware() && !device.IsVisorDown())
		{
			Announce("Remote Feed", "VISOR UP");
			return;
		}

		if (!device.IsIvasEnabled())
		{
			Announce("Remote Feed", "IVAS OFF");
			return;
		}

		device.PushFeedGeometry();
		renderer.Start();

		Announce("Remote Feed", ActiveSourceName());
	}

	//------------------------------------------------------------------------------------------------
	// Cycles. Written out rather than done with modular arithmetic on the enum, because an enum
	// that gains a value later would silently start cycling through it.
	//------------------------------------------------------------------------------------------------
	protected ACM_TDFriendlyIffMode NextScope(ACM_TDFriendlyIffMode mode)
	{
		if (mode == ACM_TDFriendlyIffMode.GROUP_LEADER)
			return ACM_TDFriendlyIffMode.GROUP;

		if (mode == ACM_TDFriendlyIffMode.GROUP)
			return ACM_TDFriendlyIffMode.ALL_FRIENDLIES;

		// ALL and OFF both land on LEADER. OFF is reachable only through the Friendly IFF master,
		// so the scope cycle never has to produce it - a scope of OFF with the master ON would be
		// two switches saying the same thing.
		return ACM_TDFriendlyIffMode.GROUP_LEADER;
	}

	//------------------------------------------------------------------------------------------------
	protected ACM_TDIffDetail NextIffDetail(ACM_TDIffDetail detail)
	{
		if (detail == ACM_TDIffDetail.MINIMAL)
			return ACM_TDIffDetail.STANDARD;

		if (detail == ACM_TDIffDetail.STANDARD)
			return ACM_TDIffDetail.DETAILED;

		return ACM_TDIffDetail.MINIMAL;
	}

	//------------------------------------------------------------------------------------------------
	protected ACM_TDContactDetail NextContactDetail(ACM_TDContactDetail detail)
	{
		if (detail == ACM_TDContactDetail.MINIMAL)
			return ACM_TDContactDetail.STANDARD;

		if (detail == ACM_TDContactDetail.STANDARD)
			return ACM_TDContactDetail.DETAILED;

		return ACM_TDContactDetail.MINIMAL;
	}

	//------------------------------------------------------------------------------------------------
	protected ACM_TDContactFilter NextFilter(ACM_TDContactFilter filter)
	{
		if (filter == ACM_TDContactFilter.ALL)
			return ACM_TDContactFilter.RELEVANT;

		if (filter == ACM_TDContactFilter.RELEVANT)
			return ACM_TDContactFilter.THREATS;

		return ACM_TDContactFilter.ALL;
	}

	//------------------------------------------------------------------------------------------------
	protected ACM_TDMarkerScalePreset NextScale(ACM_TDMarkerScalePreset preset)
	{
		if (preset == ACM_TDMarkerScalePreset.SMALL)
			return ACM_TDMarkerScalePreset.NORMAL;

		if (preset == ACM_TDMarkerScalePreset.NORMAL)
			return ACM_TDMarkerScalePreset.LARGE;

		return ACM_TDMarkerScalePreset.SMALL;
	}

	//------------------------------------------------------------------------------------------------
	protected ACM_TDFeedSizePreset NextFeedSize(ACM_TDFeedSizePreset preset)
	{
		if (preset == ACM_TDFeedSizePreset.SMALL)
			return ACM_TDFeedSizePreset.MEDIUM;

		if (preset == ACM_TDFeedSizePreset.MEDIUM)
			return ACM_TDFeedSizePreset.LARGE;

		return ACM_TDFeedSizePreset.SMALL;
	}

	//------------------------------------------------------------------------------------------------
	protected ACM_TDFeedPositionPreset NextFeedPosition(ACM_TDFeedPositionPreset preset)
	{
		if (preset == ACM_TDFeedPositionPreset.TOP_LEFT)
			return ACM_TDFeedPositionPreset.TOP_RIGHT;

		if (preset == ACM_TDFeedPositionPreset.TOP_RIGHT)
			return ACM_TDFeedPositionPreset.BOTTOM_RIGHT;

		if (preset == ACM_TDFeedPositionPreset.BOTTOM_RIGHT)
			return ACM_TDFeedPositionPreset.BOTTOM_LEFT;

		return ACM_TDFeedPositionPreset.TOP_LEFT;
	}

	//------------------------------------------------------------------------------------------------
	// Performing an entry closes the radial, so without this the player would have to reopen the
	// menu to find out what the value became. Cheap, and it is what makes cycle-on-select usable.
	//------------------------------------------------------------------------------------------------
	protected void Announce(string setting, string value)
	{
		SCR_HintManagerComponent.ShowCustomHint(string.Format("%1: %2", setting, value), "ACM IVAS", 2);
	}
}

//------------------------------------------------------------------------------------------------
// One authorised remote feed source.
//
// Selection goes back through the renderer's authorisation list rather than trusting this handle.
// A menu entry is built when the ring opens and performed some seconds later, by which time the
// source may have left the viewer's squad, been unlinked, or been destroyed.
//------------------------------------------------------------------------------------------------
class ACM_FeedSourceEntry : SCR_SelectionMenuEntry
{
	protected ACM_RemoteFeedSourceComponent m_Source;
	protected string m_sCallsign;
	protected bool m_bPerformed;

	//------------------------------------------------------------------------------------------------
	void ACM_FeedSourceEntry(ACM_RemoteFeedSourceComponent source, string callsign)
	{
		m_Source = source;
		m_sCallsign = callsign;
		SetIcon(ACM_RADIAL_IMAGESET, "category");
		Update();
	}

	//------------------------------------------------------------------------------------------------
	override void Update()
	{
		if (m_Source && ACM_RemoteFeedRenderer.GetInstance().GetSource() == m_Source)
			SetName(string.Format("%1 [ACTIVE]", m_sCallsign));
		else
			SetName(m_sCallsign);
	}

	//------------------------------------------------------------------------------------------------
	override void OnPerform()
	{
		if (m_bPerformed)
			return;

		m_bPerformed = true;

		if (!m_Source)
			return;

		ACM_RemoteFeedRenderer renderer = ACM_RemoteFeedRenderer.GetInstance();

		if (!renderer.SelectSource(m_Source))
		{
			SCR_HintManagerComponent.ShowCustomHint(string.Format("%1 is no longer available", m_sCallsign), "ACM IVAS", 3);
			return;
		}

		SCR_HintManagerComponent.ShowCustomHint(string.Format("Remote Feed: %1", m_sCallsign), "ACM IVAS", 2);
	}
}

//------------------------------------------------------------------------------------------------
// Shown when the viewer is authorised for nothing.
//
// A deliberate entry rather than an empty ring. An empty submenu is indistinguishable from a broken
// one, and "no sources" is a real answer worth telling the player.
//------------------------------------------------------------------------------------------------
class ACM_NoFeedSourceEntry : SCR_SelectionMenuEntry
{
	void ACM_NoFeedSourceEntry()
	{
		SetIcon(ACM_RADIAL_IMAGESET, "category");
		SetName("No Sources");
	}

	override void Update()
	{
		SetName("No Sources");
	}

	override void OnPerform()
	{
	}
}
