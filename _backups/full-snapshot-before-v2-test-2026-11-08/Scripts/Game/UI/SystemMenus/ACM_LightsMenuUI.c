//------------------------------------------------------------------------------------------------
// ACM_LightsMenuUI.c
// Every light the player is carrying, on one screen.
//
// Scope is "lights that are not lasers": helmet and equipment lights, weapon lights, and strobes.
// A device carrying ACM_NVL_WeaponLaserComponent belongs to ACM_LasersMenuUI instead, so the two
// menus partition the loadout rather than overlapping - a PEQ appearing in both would leave the
// player guessing which copy of the toggle is the real one.
//------------------------------------------------------------------------------------------------
class ACM_LightsMenuUI : ACM_SystemMenuBase
{
	//------------------------------------------------------------------------------------------------
	override protected string MenuTitle() { return "ACM LIGHTS"; }

	//------------------------------------------------------------------------------------------------
	override protected string MenuHint()
	{
		return "Weapon lights, equipment lights and strobes. Lasers have their own menu.";
	}

	//------------------------------------------------------------------------------------------------
	override protected void BuildRows()
	{
		array<IEntity> devices = {};
		CollectAllDevices(devices);

		int shown = 0;

		foreach (IEntity device : devices)
		{
			if (IsLaserDevice(device))
				continue;

			int before = m_aRows.Count();
			string name = DeviceName(device);

			AddHeader(name);
			AddLightRows(device, name);
			AddStrobeRows(device, name);

			// Nothing but the header went in, so the header is describing an empty section. Drop it
			// rather than showing a device that offers no controls.
			if (m_aRows.Count() == before + 1)
				m_aRows.Remove(before);
			else
				shown++;
		}

		if (shown == 0)
		{
			SetSubtitle("No lights or strobes in your loadout.");
			return;
		}

		SetSubtitle(string.Format("%1 device(s) with lights.", shown));
	}

	//------------------------------------------------------------------------------------------------
	// A laser device is one carrying ACM_NVL_WeaponLaserComponent. Tested on the device rather than
	// per component so a combined light-and-laser unit lands in exactly one menu.
	//------------------------------------------------------------------------------------------------
	protected bool IsLaserDevice(IEntity device)
	{
		if (!device)
			return true;

		return ACM_NVL_WeaponLaserComponent.Cast(device.FindComponent(ACM_NVL_WeaponLaserComponent)) != null;
	}

	//------------------------------------------------------------------------------------------------
	protected void AddLightRows(IEntity device, string name)
	{
		array<Managed> lightComponents = {};
		device.FindComponents(ACM_NVL_LightsComponent, lightComponents);

		bool hasFlashlight = false;
		bool hasIR = false;

		foreach (Managed managed : lightComponents)
		{
			ACM_NVL_LightsComponent comp = ACM_NVL_LightsComponent.Cast(managed);
			if (!comp)
				continue;

			if (comp.HasLightType(ACM_NVL_LightType.FLASHLIGHT))
				hasFlashlight = true;

			if (comp.HasLightType(ACM_NVL_LightType.IRLIGHT))
				hasIR = true;
		}

		if (hasFlashlight)
			AddAction(new ACM_DynamicMenuEntry(device, ACM_DynamicActionType.TOGGLE_FLASHLIGHT, name));

		if (hasIR)
		{
			AddAction(new ACM_DynamicMenuEntry(device, ACM_DynamicActionType.TOGGLE_IR, name));
			AddAction(new ACM_DynamicMenuEntry(device, ACM_DynamicActionType.TOGGLE_IR_FILTER, name));
		}

		// One cycling row rather than eight colour rows. The radial has room for a colour ring;
		// this list does not, and a device that ate eight lines would crowd out the next device.
		if (hasFlashlight || hasIR)
			AddAction(new ACM_DynamicMenuEntry(device, ACM_DynamicActionType.CYCLE_LIGHT_COLOR, name));
	}

	//------------------------------------------------------------------------------------------------
	protected void AddStrobeRows(IEntity device, string name)
	{
		ACM_NVL_StrobeComponent strobe = ACM_NVL_StrobeComponent.Cast(device.FindComponent(ACM_NVL_StrobeComponent));
		if (!strobe)
			return;

		AddAction(new ACM_DynamicMenuEntry(device, ACM_DynamicActionType.TOGGLE_STROBE, name));
		AddAction(new ACM_DynamicMenuEntry(device, ACM_DynamicActionType.TOGGLE_STROBE_IR_COVER, name));
	}
}
