// Server-owned IFF policy. Clients only consume their replicated character policy.
class ACM_IffServerSettingsData
{
	bool iffEnabled = true;
	bool friendlyIffEnabled = true;
	bool hostileTrackingEnabled = true;
	bool civilianIffEnabled = true;
	bool unknownContactsEnabled = true;
	bool friendlyOutlinesEnabled = true;
	bool hostileOutlinesEnabled = true;
	bool contactSharingEnabled = true;
}

class ACM_IffServerSettings
{
	static const string CONFIG_PATH = "$profile:ACM_IVAS_IFF.json";
	static const int MASTER = 1;
	static const int FRIENDLY = 2;
	static const int HOSTILE = 4;
	static const int CIVILIAN = 8;
	static const int UNKNOWN = 16;
	static const int FRIENDLY_OUTLINE = 32;
	static const int HOSTILE_OUTLINE = 64;
	static const int DATA_LINK = 128;
	protected static BaseWorld s_World;
	protected static ref ACM_IffServerSettingsData s_Data;

	// Load once per world on the authority; never read a connecting client's profile.
	static int ServerMask()
	{
		if (Replication.IsRunning() && !Replication.IsServer())
			return 0;
		BaseWorld world = GetGame().GetWorld();
		if (!s_Data || s_World != world)
		{
			s_World = world;
			s_Data = new ACM_IffServerSettingsData();
			if (FileIO.FileExists(CONFIG_PATH))
			{
				SCR_JsonLoadContext reader = new SCR_JsonLoadContext();
				if (!reader.LoadFromFile(CONFIG_PATH) || !reader.ReadValue("", s_Data))
				{
					s_Data.iffEnabled = false;
					Print("[ACM IVAS IFF] Invalid ACM_IVAS_IFF.json; IFF disabled. Correct the file and restart the server. File preserved.", LogLevel.ERROR);
				}
			}
			else
			{
				SCR_JsonSaveContext writer = new SCR_JsonSaveContext();
				writer.WriteValue("", s_Data);
				writer.SaveToFile(CONFIG_PATH);
			}
			Print("[ACM IVAS IFF] Loaded server policy from " + CONFIG_PATH, LogLevel.NORMAL);
		}
		return BuildMask(s_Data);
	}

	static int BuildMask(ACM_IffServerSettingsData data)
	{
		if (!data || !data.iffEnabled)
			return 0;
		int mask = MASTER;
		if (data.friendlyIffEnabled) mask = mask | FRIENDLY;
		if (data.hostileTrackingEnabled) mask = mask | HOSTILE;
		if (data.civilianIffEnabled) mask = mask | CIVILIAN;
		if (data.unknownContactsEnabled) mask = mask | UNKNOWN;
		if (data.friendlyOutlinesEnabled) mask = mask | FRIENDLY_OUTLINE;
		if (data.hostileOutlinesEnabled) mask = mask | HOSTILE_OUTLINE;
		if (data.contactSharingEnabled) mask = mask | DATA_LINK;
		return mask;
	}

	static bool Allows(int feature)
	{
		int mask;
		if (!Replication.IsRunning() || Replication.IsServer())
			mask = ServerMask();
		else
		{
			IEntity controlled = SCR_PlayerController.GetLocalControlledEntity();
			if (!controlled)
				return false;
			ACM_PlayerLinkComponent link = ACM_PlayerLinkComponent.Cast(controlled.FindComponent(ACM_PlayerLinkComponent));
			if (!link)
				return false;
			mask = link.GetServerIffMask();
		}
		return (mask & MASTER) != 0 && (mask & feature) == feature;
	}
}
