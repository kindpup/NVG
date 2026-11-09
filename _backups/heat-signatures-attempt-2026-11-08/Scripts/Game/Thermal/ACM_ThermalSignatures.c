//------------------------------------------------------------------------------------------------
// Heat signatures while thermal is on.
//
// Vanilla gives nothing an internal temperature: the engine's thermal effect only simulates sun
// heating, so people and running engines read as cold as the ground. The engine does support extra
// temperatures per MATERIAL, though, and materials are local to this machine - the same lever as
// ACM_ThermalClouds. While any thermal device is on:
//
//  - Characters: every character near the camera has its materials (body, clothing, gear - the
//    whole entity hierarchy) given a body temperature through the material's TITemperature0 /
//    TITemperatureCoef0 parameters.
//  - Exhaust: every vanilla vehicle exhaust (cars, trucks, BTR, BRDM, LAV, helicopters) uses one
//    particle material, Smoke_Exhaust_Anim.emat. Giving it a temperature makes the exhaust plume of
//    every running engine glow; engines that are off emit no smoke and so show nothing.
//
// Everything is handed back (ResetParam) when the last thermal device turns off. Nothing is
// replicated; other players are unaffected.
//
// Limits: a material is shared by everything that uses it, so the same clothing/gear on a dead body
// or lying on the ground is warm too while thermal is on. Prefabs that swap materials on a mesh
// (MeshObject material remaps) keep the remapped material cold - only the model's own materials are
// visible to script.
//------------------------------------------------------------------------------------------------
class ACM_ThermalSignatures
{
	protected static const ResourceName EXHAUST_MATERIAL = "{106C63A94553E961}Particles/_Materials/Gas/Smoke_Exhaust_Anim.emat";

	//! Body temperature given to character materials (degrees Celsius).
	protected static const float CHARACTER_TEMPERATURE = 37;
	//! How strongly the character temperature replaces the simulated one (0..1).
	protected static const float CHARACTER_COEF = 1.0;
	//! Exhaust plume temperature (degrees Celsius).
	protected static const float EXHAUST_TEMPERATURE = 150;
	//! Faint emission given to the exhaust smoke, and how strongly it converts into thermal strength.
	protected static const float EXHAUST_EMISSION = 0.02;
	protected static const float EXHAUST_EMISSION_TO_TEMP = 500;

	//! How far from the camera characters are picked up, and how often (seconds).
	protected static const float SCAN_RADIUS = 600;
	protected static const int SCAN_INTERVAL_MS = 1000;
	//! Cap on newly heated materials per scan, so a crowded area cannot cause a hitch.
	protected static const int MAX_NEW_MATERIALS_PER_SCAN = 64;

	protected static ref array<Managed> s_aOwners;
	protected static bool s_bActive;

	//! Materials currently heated (by resource name), so each is set once and reset on exit.
	protected static ref map<string, ref Material> s_mHeated;
	//! Every material instance given a temperature (a name can resolve to several), for the reset.
	protected static ref array<ref Material> s_aHeatedMaterials;
	//! Material names that could not be resolved, plus a few samples for the log.
	protected static ref map<string, bool> s_mFailed;
	protected static ref array<string> s_aSampleFailed;
	protected static int s_iParamRejected;
	//! Which TITemperature slot the engine accepts (-1 = not found yet), and whether it was diagnosed.
	protected static int s_iWorkingSlot = -1;
	protected static bool s_bParamsDiagnosed;
	protected static bool s_bScanLogged;
	protected static ref array<IEntity> s_aQuery;
	protected static ref Material s_ExhaustMaterial;
	protected static bool s_bLogged;

	//------------------------------------------------------------------------------------------------
	//! Turn heat signatures on or off for one thermal device. They stay on while any owner is active.
	static void SetActive(Managed owner, bool active)
	{
		if (!owner)
			return;

		if (!s_aOwners)
			s_aOwners = {};

		int index = s_aOwners.Find(owner);
		if (active && index < 0)
			s_aOwners.Insert(owner);
		else if (!active && index >= 0)
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
	protected static void Start()
	{
		if (!s_mHeated)
			s_mHeated = new map<string, ref Material>();
		if (!s_mFailed)
			s_mFailed = new map<string, bool>();
		if (!s_aHeatedMaterials)
			s_aHeatedMaterials = {};
		if (!s_aSampleFailed)
			s_aSampleFailed = {};
		if (!s_aQuery)
			s_aQuery = {};
		s_iParamRejected = 0;
		s_bScanLogged = false;

		// Exhaust plumes. The particle's thermal strength (TempCoef) normally comes from a curve in
		// the emitter, and the vanilla exhaust effects have none - so a temperature on its own does
		// nothing. EmissionToTempCoef derives TempCoef from the particle's emission instead, so the
		// smoke gets a barely visible emission and a strong conversion factor.
		bool exhaustOk;
		bool emissionOk;
		bool emissionLvOk;
		if (!s_ExhaustMaterial)
			s_ExhaustMaterial = Material.GetOrLoadMaterial(EXHAUST_MATERIAL, 0);
		if (s_ExhaustMaterial)
		{
			exhaustOk = s_ExhaustMaterial.SetParam("Temperature", EXHAUST_TEMPERATURE);
			s_ExhaustMaterial.SetParam("TempCoefMult", 1.0);
			s_ExhaustMaterial.SetParam("EmissionToTempCoef", EXHAUST_EMISSION_TO_TEMP);

			// The emissive colour's script type is not documented: try a vector, then a Color.
			emissionOk = s_ExhaustMaterial.SetParam("Emissive", Vector(EXHAUST_EMISSION, EXHAUST_EMISSION, EXHAUST_EMISSION));
			if (!emissionOk)
				emissionOk = s_ExhaustMaterial.SetParam("Emissive", new Color(EXHAUST_EMISSION, EXHAUST_EMISSION, EXHAUST_EMISSION, 1));

			// Emission brightness (light value); 0 keeps it near-invisible with the faint colour above.
			emissionLvOk = s_ExhaustMaterial.SetParam("EmissiveLV", 0.0);
		}

		Print("[ACM Thermal] Exhaust heat: temperature set: " + exhaustOk.ToString() + ", emission colour set: " + emissionOk.ToString() + ", emission LV set: " + emissionLvOk.ToString() + ".", LogLevel.NORMAL);

		Scan();

		if (GetGame() && GetGame().GetCallqueue())
		{
			GetGame().GetCallqueue().Remove(Scan);
			GetGame().GetCallqueue().CallLater(Scan, SCAN_INTERVAL_MS, true);
		}

		LogScan();
	}

	//------------------------------------------------------------------------------------------------
	protected static void LogScan()
	{
		string samples;
		foreach (string sample : s_aSampleFailed)
		{
			if (!samples.IsEmpty())
				samples += ", ";
			samples += sample;
		}

		Print("[ACM Thermal] Heat signatures: character materials heated: " + s_mHeated.Count().ToString() + ", not found: " + s_mFailed.Count().ToString() + ", temperature params rejected: " + s_iParamRejected.ToString() + ". Not found e.g.: " + samples, LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	protected static void Stop()
	{
		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().Remove(Scan);

		if (s_ExhaustMaterial)
		{
			s_ExhaustMaterial.ResetParam("Temperature");
			s_ExhaustMaterial.ResetParam("TempCoefMult");
			s_ExhaustMaterial.ResetParam("EmissionToTempCoef");
			s_ExhaustMaterial.ResetParam("Emissive");
			s_ExhaustMaterial.ResetParam("EmissiveLV");
		}

		int restored;
		if (s_aHeatedMaterials)
		{
			foreach (Material material : s_aHeatedMaterials)
			{
				if (!material)
					continue;

				material.ResetParam("TITemperature0");
				material.ResetParam("TITemperatureCoef0");
				restored++;
			}
			s_aHeatedMaterials.Clear();
		}

		if (s_mHeated)
			s_mHeated.Clear();

		if (s_mFailed)
			s_mFailed.Clear();
		if (s_aSampleFailed)
			s_aSampleFailed.Clear();

		s_bLogged = false;
		Print("[ACM Thermal] Heat signatures off: " + restored.ToString() + " character materials restored.", LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	//! Picks up characters near the camera and heats any of their materials not heated yet.
	protected static void Scan()
	{
		if (!s_bActive || !GetGame())
			return;

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return;

		vector camera[4];
		world.GetCurrentCamera(camera);

		s_aQuery.Clear();
		world.QueryEntitiesBySphere(camera[3], SCAN_RADIUS, AddCharacter, FilterCharacter, EQueryEntitiesFlags.ALL);

		int added;
		foreach (IEntity character : s_aQuery)
		{
			if (added >= MAX_NEW_MATERIALS_PER_SCAN)
				break;

			added += HeatHierarchy(character, MAX_NEW_MATERIALS_PER_SCAN - added, 0);
		}

		// Report once after the first scan that found characters (more may appear later).
		if (!s_bScanLogged && !s_aQuery.IsEmpty())
		{
			s_bScanLogged = true;
			LogScan();
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Heats the materials of an entity and everything attached to it. Returns how many were new.
	protected static int HeatHierarchy(IEntity entity, int budget, int depth)
	{
		if (!entity || budget <= 0 || depth > 8)
			return 0;

		int added = HeatEntityMaterials(entity, budget);

		IEntity child = entity.GetChildren();
		while (child && added < budget)
		{
			added += HeatHierarchy(child, budget - added, depth + 1);
			child = child.GetSibling();
		}

		return added;
	}

	//------------------------------------------------------------------------------------------------
	protected static int HeatEntityMaterials(IEntity entity, int budget)
	{
		VObject mesh = entity.GetVObject();
		if (!mesh)
			return 0;

		string materials[256];
		int count = mesh.GetMaterials(materials);
		int added;

		for (int i = 0; i < count && added < budget; i++)
		{
			string name = materials[i];
			if (name.IsEmpty() || s_mHeated.Contains(name) || s_mFailed.Contains(name))
				continue;

			// GetMaterials returns the material's NAME (e.g. "Basebody_01_Male_Body", the material
			// actually drawn, remaps included). A material can only be fetched by its full resource
			// name, so the name is looked up in the generated index (ACM_ThermalMaterialIndex).
			array<ref Material> found = ResolveMaterials(name);
			if (found.IsEmpty())
			{
				// Remember it so it is not retried every scan.
				s_mFailed.Insert(name, true);
				if (s_aSampleFailed.Count() < 5)
					s_aSampleFailed.Insert(name);
				continue;
			}

			foreach (Material material : found)
			{
				if (!HeatMaterial(material, name))
					s_iParamRejected++;

				s_aHeatedMaterials.Insert(material);
			}

			s_mHeated.Insert(name, found[0]);
			added++;
		}

		return added;
	}

	//------------------------------------------------------------------------------------------------
	//! Gives one material the body temperature. The thermal parameters are listed in the engine as
	//! "TITemperature%d" / "TITemperatureCoef%d"; whether %d starts at 0 or 1 is not documented, so both
	//! are tried and the slot that works is remembered. The first material also logs which parameter
	//! names exist on it (GetParamIndex), so a rejection can be diagnosed from the log.
	protected static bool HeatMaterial(Material material, string name)
	{
		if (!s_bParamsDiagnosed)
		{
			s_bParamsDiagnosed = true;
			string report;
			foreach (string candidate : {"TITemperature0", "TITemperature1", "TITemperatureCoef0", "TITemperatureCoef1", "TITemperatureOp0", "TITemperatureCombineOp", "TIThermalProfile", "TIMap"})
				report += " " + candidate + "=" + material.GetParamIndex(candidate).ToString();

			Print("[ACM Thermal] Thermal parameter indices on '" + name + "':" + report, LogLevel.NORMAL);
		}

		if (s_iWorkingSlot >= 0)
			return SetTemperature(material, s_iWorkingSlot);

		for (int slot = 0; slot <= 1; slot++)
		{
			if (SetTemperature(material, slot))
			{
				s_iWorkingSlot = slot;
				Print("[ACM Thermal] Character heat uses TITemperature" + slot.ToString() + ".", LogLevel.NORMAL);
				return true;
			}
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool SetTemperature(Material material, int slot)
	{
		bool tempOk = material.SetParam("TITemperature" + slot.ToString(), CHARACTER_TEMPERATURE);
		bool coefOk = material.SetParam("TITemperatureCoef" + slot.ToString(), CHARACTER_COEF);
		return tempOk && coefOk;
	}

	//------------------------------------------------------------------------------------------------
	//! Material instances for a material name: the copies already loaded (i.e. in use), or - if none is
	//! loaded yet - the first one that loads.
	protected static array<ref Material> ResolveMaterials(string name)
	{
		array<ref Material> result = {};

		array<string> resources = ACM_ThermalMaterialIndex.Find(name);
		if (!resources)
			return result;

		foreach (string resource : resources)
		{
			Material loaded = Material.GetMaterial(resource);
			if (loaded)
				result.Insert(loaded);
		}

		if (!result.IsEmpty())
			return result;

		foreach (string resource : resources)
		{
			Material material = Material.GetOrLoadMaterial(resource, 0);
			if (material)
			{
				result.Insert(material);
				break;
			}
		}

		return result;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool FilterCharacter(IEntity entity)
	{
		return ChimeraCharacter.Cast(entity) != null;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool AddCharacter(IEntity entity)
	{
		if (entity)
			s_aQuery.Insert(entity);

		return true;
	}
}
