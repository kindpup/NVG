//------------------------------------------------------------------------------------------------
// ACM_NVGMenuUI.c
// Night vision: power and phosphor, per device.
//
// Phosphor is listed one colour per row rather than as a cycling entry, because unlike a light
// colour the player usually wants a specific tube tint and not "the next one". The colour in use is
// marked so the list reads as a state display as well as a control.
//------------------------------------------------------------------------------------------------
class ACM_NVGMenuUI : ACM_SystemMenuBase
{
	//------------------------------------------------------------------------------------------------
	override protected string MenuTitle() { return "ACM NIGHT VISION"; }

	//------------------------------------------------------------------------------------------------
	override protected string MenuHint()
	{
		return "Power and phosphor colour. Brightness stays on the keybinds.";
	}

	//------------------------------------------------------------------------------------------------
	override protected void BuildRows()
	{
		array<IEntity> devices = {};
		CollectAllDevices(devices);

		int shown = 0;

		foreach (IEntity device : devices)
		{
			ACM_NightVisionComponent nvg = ACM_NightVisionComponent.Cast(device.FindComponent(ACM_NightVisionComponent));
			if (!nvg)
				continue;

			string name = DeviceName(device);

			AddHeader(name);
			AddAction(new ACM_DynamicMenuEntry(device, ACM_DynamicActionType.TOGGLE_NVG, name));

			// A picker listing one choice is a dead entry - the same test the radial makes.
			if (nvg.GetAvailableColorCount() > 1)
			{
				array<string> colorNames = nvg.GetAvailableColorNames();
				int current = nvg.GetCurrentColorIndex();

				for (int i = 0; i < colorNames.Count(); i++)
				{
					string suffix = "";
					if (i == current)
						suffix = "[CURRENT]";

					AddAction(new ACM_PhosphorColorEntry(device, i, colorNames[i]), suffix);
				}
			}

			shown++;
		}

		if (shown == 0)
		{
			SetSubtitle("No night vision device in your loadout.");
			return;
		}

		SetSubtitle(string.Format("%1 night vision device(s).", shown));
	}
}
