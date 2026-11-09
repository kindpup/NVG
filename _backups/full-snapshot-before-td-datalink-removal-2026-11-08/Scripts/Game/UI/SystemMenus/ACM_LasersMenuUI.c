//------------------------------------------------------------------------------------------------
// ACM_LasersMenuUI.c
// Weapon lasers only - the devices carrying ACM_NVL_WeaponLaserComponent.
//
// Same rows as the lights menu, but every entry is built with colorPreferWeaponLaser set, so the
// entry resolves the laser component on a unit that carries both a light and a laser rather than
// silently operating the wrong emitter.
//------------------------------------------------------------------------------------------------
class ACM_LasersMenuUI : ACM_SystemMenuBase
{
	//------------------------------------------------------------------------------------------------
	override protected string MenuTitle() { return "ACM LASERS"; }

	//------------------------------------------------------------------------------------------------
	override protected string MenuHint()
	{
		return "Visible and IR laser emitters on your weapon.";
	}

	//------------------------------------------------------------------------------------------------
	override protected void BuildRows()
	{
		array<IEntity> devices = {};
		CollectAllDevices(devices);

		int shown = 0;

		foreach (IEntity device : devices)
		{
			if (!ACM_NVL_WeaponLaserComponent.Cast(device.FindComponent(ACM_NVL_WeaponLaserComponent)))
				continue;

			string name = DeviceName(device);

			array<Managed> lightComponents = {};
			device.FindComponents(ACM_NVL_LightsComponent, lightComponents);

			bool hasVisible = false;
			bool hasIR = false;

			foreach (Managed managed : lightComponents)
			{
				ACM_NVL_LightsComponent comp = ACM_NVL_LightsComponent.Cast(managed);
				if (!comp)
					continue;

				if (comp.HasLightType(ACM_NVL_LightType.FLASHLIGHT))
					hasVisible = true;

				if (comp.HasLightType(ACM_NVL_LightType.IRLIGHT))
					hasIR = true;
			}

			if (!hasVisible && !hasIR)
				continue;

			AddHeader(name);

			if (hasVisible)
				AddAction(new ACM_DynamicMenuEntry(device, ACM_DynamicActionType.TOGGLE_FLASHLIGHT, name, false, true));

			if (hasIR)
			{
				AddAction(new ACM_DynamicMenuEntry(device, ACM_DynamicActionType.TOGGLE_IR, name, true, true));
				AddAction(new ACM_DynamicMenuEntry(device, ACM_DynamicActionType.TOGGLE_IR_FILTER, name, true, true));
			}

			AddAction(new ACM_DynamicMenuEntry(device, ACM_DynamicActionType.CYCLE_LIGHT_COLOR, name, false, true));

			shown++;
		}

		if (shown == 0)
		{
			SetSubtitle("No laser devices on your weapon.");
			return;
		}

		SetSubtitle(string.Format("%1 laser device(s).", shown));
	}
}
