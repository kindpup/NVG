//------------------------------------------------------------------------------------------------
// ACM_ThermalMenuUI.c
// Thermal: power and palette, per device.
//
// Palette names are free text set per device in the prefab, so they are listed exactly as the
// component reports them rather than mapped to a fixed vocabulary here.
//------------------------------------------------------------------------------------------------
class ACM_ThermalMenuUI : ACM_SystemMenuBase
{
	//------------------------------------------------------------------------------------------------
	override protected string MenuTitle() { return "ACM THERMAL"; }

	//------------------------------------------------------------------------------------------------
	override protected string MenuHint()
	{
		return "Power and palette. Brightness stays on the keybinds.";
	}

	//------------------------------------------------------------------------------------------------
	override protected void BuildRows()
	{
		array<IEntity> devices = {};
		CollectAllDevices(devices);

		int shown = 0;

		foreach (IEntity device : devices)
		{
			ACM_ThermalComponentV2 thermal = ACM_ThermalComponentV2.Cast(device.FindComponent(ACM_ThermalComponentV2));
			if (!thermal)
				continue;

			string name = DeviceName(device);

			AddHeader(name);
			AddAction(new ACM_DynamicMenuEntry(device, ACM_DynamicActionType.TOGGLE_THERMAL, name));

			if (thermal.GetPaletteCount() > 1)
			{
				array<string> paletteNames = thermal.GetPaletteNames();
				int current = thermal.GetCurrentPaletteIndex();

				for (int i = 0; i < paletteNames.Count(); i++)
				{
					string suffix = "";
					if (i == current)
						suffix = "[CURRENT]";

					AddAction(new ACM_ThermalPaletteEntry(device, i, paletteNames[i]), suffix);
				}
			}

			shown++;
		}

		if (shown == 0)
		{
			SetSubtitle("No thermal device in your loadout.");
			return;
		}

		SetSubtitle(string.Format("%1 thermal device(s).", shown));
	}
}
