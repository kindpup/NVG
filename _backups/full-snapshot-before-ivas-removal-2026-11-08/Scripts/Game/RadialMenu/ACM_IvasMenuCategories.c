//------------------------------------------------------------------------------------------------
// ACM_IvasMenuCategories.c
// The six IVAS branches, the reset confirmation, and the remote feed source picker.
//
// WHY THESE SIT ON THE ROOT RING. Putting them one level down behind a single "IVAS" wedge would
// give a tidier root and push every value picker to a fourth ring, and the depth limit for this
// menu is three. Controller usability wins that trade: a busier root costs one stick movement to
// read, a fourth ring costs one on every single setting.
//
// The root stays manageable because an IVAS device no longer contributes its own device category -
// its visor, night vision, phosphor and thermal entries live under SYSTEM instead, so the device
// appears once rather than twice. Weapon lights, lasers and strobes keep their own categories,
// which is right: they are separate hardware and they are not IVAS.
//
// Every category's own label carries live state, so a player can read what the branch is set to
// without opening it.
//------------------------------------------------------------------------------------------------
enum ACM_EIvasCategory
{
	SYSTEM,
	IFF,
	TARGETING,
	DATA_LINK,
	REMOTE_FEED,
	DISPLAY,
	RESET_CONFIRMATION
}

//------------------------------------------------------------------------------------------------
class ACM_IvasCategory : SCR_SelectionMenuCategoryEntry
{
	protected ACM_EIvasCategory m_eCategory;

	//------------------------------------------------------------------------------------------------
	void ACM_IvasCategory(ACM_EIvasCategory category)
	{
		m_eCategory = category;
		SetIcon(ACM_RADIAL_IMAGESET, IconName());
		Update();
	}

	//------------------------------------------------------------------------------------------------
	protected ACM_TargetDetectionComponent Device()
	{
		return ACM_TargetDetectionComponent.GetLocalDevice();
	}

	//------------------------------------------------------------------------------------------------
	protected string IconName()
	{
		if (m_eCategory == ACM_EIvasCategory.SYSTEM)
			return "nvg";

		return "category";
	}

	//------------------------------------------------------------------------------------------------
	protected string BaseLabel()
	{
		switch (m_eCategory)
		{
			case ACM_EIvasCategory.SYSTEM: return "SYSTEM";
			case ACM_EIvasCategory.IFF: return "IFF";
			case ACM_EIvasCategory.TARGETING: return "TARGETING";
			case ACM_EIvasCategory.DATA_LINK: return "DATA LINK";
			case ACM_EIvasCategory.REMOTE_FEED: return "REMOTE FEED";
			case ACM_EIvasCategory.DISPLAY: return "DISPLAY";
			case ACM_EIvasCategory.RESET_CONFIRMATION: return "Reset IVAS Settings";
		}

		return "IVAS";
	}

	//------------------------------------------------------------------------------------------------
	// The one number from the branch most worth knowing without opening it. Chosen for what a
	// player checks mid-firefight, not for what is most technically interesting.
	//------------------------------------------------------------------------------------------------
	protected string StateText()
	{
		ACM_TargetDetectionComponent device = Device();
		if (!device)
			return string.Empty;

		switch (m_eCategory)
		{
			case ACM_EIvasCategory.SYSTEM:
			{
				// Reported first and deliberately. If this machine is not in control of the device -
				// mid-teleport, mid-possession, or wearing somebody else's helmet - that is the fact
				// that explains every other entry behaving oddly, so it goes on the category label
				// where it is visible without opening anything.
				if (device.HasVisorHardware() && !device.HasLocalAuthority())
					return "NO CONTROL";

				if (!device.IsIvasEnabled())
					return "IVAS OFF";

				if (device.HasVisorHardware() && !device.IsVisorDown())
					return "VISOR UP";

				return string.Empty;
			}

			case ACM_EIvasCategory.IFF:
			{
				if (!device.IsFriendlyIffEnabled())
					return "FRIENDLY OFF";

				return ScopeName(device.GetFriendlyIffScope());
			}

			case ACM_EIvasCategory.TARGETING:
			{
				if (!device.IsHostileTrackingEnabled())
					return "TRACKING OFF";

				return FilterName(device.GetContactFilter());
			}

			case ACM_EIvasCategory.DATA_LINK:
			{
				if (!device.IsDataLinkEnabled())
					return "OFF";

				ACM_TargetDetectionSystem system = ACM_TargetDetectionSystem.GetInstance();
				if (!system)
					return "ON";

				return string.Format("%1 CONTACTS", system.GetContactCount());
			}

			case ACM_EIvasCategory.REMOTE_FEED:
			{
				ACM_RemoteFeedRenderer renderer = ACM_RemoteFeedRenderer.GetInstance();
				if (!renderer.IsActive())
					return "OFF";

				ACM_RemoteFeedSourceComponent source = renderer.GetSource();
				if (!source)
					return "NO SOURCE";

				return source.GetCallsign();
			}

			case ACM_EIvasCategory.DISPLAY:
			{
				if (!device.IsHudEnabled())
					return "HUD OFF";

				return string.Empty;
			}
		}

		return string.Empty;
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
	protected string FilterName(ACM_TDContactFilter filter)
	{
		if (filter == ACM_TDContactFilter.ALL)
			return "ALL";

		if (filter == ACM_TDContactFilter.THREATS)
			return "THREATS";

		return "RELEVANT";
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
	int GetEntriesCount()
	{
		return GetEntries().Count();
	}
}

//------------------------------------------------------------------------------------------------
// The remote feed source picker.
//
// The one place in this menu that legitimately needs a third ring: the list is as long as the
// number of authorised sources, which is not a number that can be cycled through comfortably.
// Next and Previous stay on ring two for exactly that reason - the common case is stepping to the
// next camera, and that should never cost a submenu.
//
// The list is built when the radial opens, and it contains only sources this viewer is authorised
// for. Selection re-checks authorisation at the renderer, so a stale entry cannot select anything.
//------------------------------------------------------------------------------------------------
class ACM_FeedSourceCategory : SCR_SelectionMenuCategoryEntry
{
	void ACM_FeedSourceCategory()
	{
		SetIcon(ACM_RADIAL_IMAGESET, "category");
		Update();
	}

	//------------------------------------------------------------------------------------------------
	override void Update()
	{
		ACM_RemoteFeedSourceComponent source = ACM_RemoteFeedRenderer.GetInstance().GetSource();

		if (source)
			SetName(string.Format("Select Source [%1]", source.GetCallsign()));
		else
			SetName("Select Source");
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
					iconWidget.LoadImageFromSet(0, ACM_RADIAL_IMAGESET, "category");
			}
		}

		Update();
	}

	//------------------------------------------------------------------------------------------------
	// Fill from the renderer's authorised list. Never shows an empty ring - an empty submenu and a
	// broken submenu look identical from the player's side.
	//------------------------------------------------------------------------------------------------
	void Populate()
	{
		array<ACM_RemoteFeedSourceComponent> sources = {};
		int count = ACM_RemoteFeedRenderer.GetSelectableSources(sources);

		if (count == 0)
		{
			AddEntry(new ACM_NoFeedSourceEntry());
			return;
		}

		foreach (ACM_RemoteFeedSourceComponent source : sources)
		{
			if (source)
				AddEntry(new ACM_FeedSourceEntry(source, source.GetCallsign()));
		}
	}
}
