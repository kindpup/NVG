//------------------------------------------------------------------------------------------------
// Clear sky while thermal is on.
//
// The engine's thermal effect renders the volumetric clouds unstably: the sky flickers between
// warm clouds and a cold clear sky (engine bug, also in vanilla-style full-screen thermal). No
// thermal material setting fixes it, so instead the clouds are switched off while thermal is on.
//
// How: the clouds are drawn from one material (Clouds_Volumetric.emat, the CloudsPreset of the
// world). Materials are local to this machine, so changing it affects only this player - nothing is
// replicated and other players keep their weather. The weather system rewrites the cloud material
// every frame, so "no clouds" is re-applied every frame while any thermal device is on, and the
// material is handed back (ResetParam) when the last one turns off.
//
// Side effect: while thermal is on, this player sees no clouds in the normal view outside the tubes
// either (at night that is barely visible).
//------------------------------------------------------------------------------------------------
class ACM_ThermalClouds
{
	protected static const ResourceName CLOUDS_MATERIAL = "{D3683C927920C51D}Terrains/Common/Sky/Clouds/Clouds_Volumetric.emat";

	//! Material parameters (names as in Clouds_Volumetric.emat) forced to 0 while thermal is on.
	protected static const string PARAM_DENSITY = "OverallDensity";
	protected static const string PARAM_COVERAGE = "CoverageModifier";

	protected static ref array<Managed> s_aOwners;
	protected static ref Material s_Material;
	protected static bool s_bActive;
	protected static bool s_bLogged;

	//------------------------------------------------------------------------------------------------
	//! Turn the clear sky on or off for one thermal device. Clouds stay off while any owner wants them off.
	static void SetClearSky(Managed owner, bool clear)
	{
		if (!owner)
			return;

		if (!s_aOwners)
			s_aOwners = {};

		int index = s_aOwners.Find(owner);
		if (clear && index < 0)
			s_aOwners.Insert(owner);
		else if (!clear && index >= 0)
			s_aOwners.Remove(index);

		bool wantActive = !s_aOwners.IsEmpty();
		if (wantActive == s_bActive)
			return;

		s_bActive = wantActive;
		if (s_bActive)
			Start();
		else
			Stop();
	}

	//------------------------------------------------------------------------------------------------
	//! Re-applies the clear sky. Called every frame while active (call queue) and from the player
	//! camera hook, which runs late in the frame - after the weather system has written the clouds.
	static void Maintain()
	{
		if (!s_bActive)
			return;

		Material material = GetCloudMaterial();
		if (!material)
			return;

		bool densityOk = material.SetParam(PARAM_DENSITY, 0.0);
		bool coverageOk = material.SetParam(PARAM_COVERAGE, 0.0);

		if (!s_bLogged)
		{
			s_bLogged = true;
			Print("[ACM Thermal] Clear sky on: clouds hidden while thermal is on (density set: " + densityOk.ToString() + ", coverage set: " + coverageOk.ToString() + ").", LogLevel.NORMAL);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static void Start()
	{
		Maintain();

		if (GetGame() && GetGame().GetCallqueue())
		{
			GetGame().GetCallqueue().Remove(Maintain);
			GetGame().GetCallqueue().CallLater(Maintain, 0, true);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static void Stop()
	{
		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().Remove(Maintain);

		Material material = GetCloudMaterial();
		if (material)
		{
			// Back to the material's own values; the weather system takes over again next frame.
			material.ResetParam(PARAM_DENSITY);
			material.ResetParam(PARAM_COVERAGE);
		}

		s_bLogged = false;
		Print("[ACM Thermal] Clear sky off: clouds restored.", LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	protected static Material GetCloudMaterial()
	{
		if (!s_Material)
			s_Material = Material.GetOrLoadMaterial(CLOUDS_MATERIAL, 0);

		return s_Material;
	}
}
