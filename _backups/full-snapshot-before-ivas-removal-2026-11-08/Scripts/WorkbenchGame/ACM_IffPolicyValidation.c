[WorkbenchPluginAttribute(name: "ACM IVAS IFF policy validation", wbModules: {"ResourceManager"})]
class ACM_IffPolicyValidation : WorkbenchPlugin
{
	override void RunCommandline() { Run(); Workbench.Exit(0); }
	override void Run()
	{
		Print(string.Format("[ACM IFF TEST] MASK=%1", ACM_IffServerSettings.ServerMask()));
		SharedItemRef worldRef = BaseWorld.CreateWorld("ACM_IFF_Validation", "ACM_IFF_Validation");
		BaseWorld world = worldRef.GetRef();
		IEntity entity = GetGame().SpawnEntityPrefab(Resource.Load("Prefabs/Items/NVG/FPGPNVG/IVAS.et"), world);
		if (!entity) { Print("[ACM IFF TEST] FAIL: IVAS spawn", LogLevel.ERROR); return; }
		ACM_TargetDetectionComponent device = ACM_TargetDetectionComponent.Cast(entity.FindComponent(ACM_TargetDetectionComponent));
		if (!device) { Print("[ACM IFF TEST] FAIL: detection component", LogLevel.ERROR); delete entity; return; }
		device.SetFriendlyIffEnabled(true);
		device.SetHostileTrackingEnabled(true);
		device.SetCivilianIffEnabled(true);
		device.SetShowUnknownContacts(true);
		device.SetOutlinesFriendlies(true);
		device.SetOutlinesConfirmed(true);
		device.SetDataLinkEnabled(true);
		device.SetShareIntent(true);
		device.SetReceiveIntent(true);
		Print(string.Format("[ACM IFF TEST] LINK share=%1 receive=%2", device.SharesContacts(), device.ReceivesContacts()));
		Print(string.Format("[ACM IFF TEST] EFFECTIVE friendly=%1 hostile=%2 civilian=%3 unknown=%4 friendlyOutline=%5 hostileOutline=%6", device.IsFriendlyIffEnabled(), device.IsHostileTrackingEnabled(), device.IsCivilianIffEnabled(), device.ShowsUnknownContacts(), device.OutlinesFriendlies(), device.OutlinesConfirmedContacts()));
		delete entity;
	}
}
