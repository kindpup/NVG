//------------------------------------------------------------------------------------------------
// Lookup table: material name -> full material resource names.
//
// The engine reports the materials on a mesh by NAME only ("Basebody_01_Male_Body"), and a material
// can only be fetched with its full resource name ("{GUID}path.emat"). Script has no way to search
// the resource database at runtime, so Configs/Thermal/ACM_ThermalMaterialIndex.conf lists the
// character / equipment materials of the base game and the installed character mods, generated
// offline from their resourceDatabase.rdb files. Regenerate it when new clothing mods are added.
//------------------------------------------------------------------------------------------------
[BaseContainerProps(configRoot: true)]
class ACM_ThermalMaterialIndex
{
	[Attribute(desc: "Full material resource names ({GUID}path.emat) that may be worn by characters.")]
	ref array<string> m_aMaterials;

	protected static const ResourceName INDEX_CONFIG = "{1E90893054FDE372}Configs/Thermal/ACM_ThermalMaterialIndex.conf";

	//! Lower-case material name -> resource names with that name (copies in several mods share names).
	protected static ref map<string, ref array<string>> s_mByName;
	protected static bool s_bLoaded;

	//------------------------------------------------------------------------------------------------
	//! Resource names for a material name, or null if it is not in the index.
	static array<string> Find(string materialName)
	{
		if (!s_bLoaded)
			Load();

		if (!s_mByName)
			return null;

		string key = materialName;
		key.ToLower();
		return s_mByName.Get(key);
	}

	//------------------------------------------------------------------------------------------------
	static int Count()
	{
		if (!s_bLoaded)
			Load();

		if (!s_mByName)
			return 0;

		return s_mByName.Count();
	}

	//------------------------------------------------------------------------------------------------
	protected static void Load()
	{
		s_bLoaded = true;
		s_mByName = new map<string, ref array<string>>();

		ACM_ThermalMaterialIndex index = ACM_ThermalMaterialIndex.Cast(SCR_BaseContainerTools.CreateInstanceFromPrefab(INDEX_CONFIG, true));
		if (!index || !index.m_aMaterials)
		{
			Print("[ACM Thermal] Material index could not be loaded (" + INDEX_CONFIG + ") - characters will not show heat.", LogLevel.WARNING);
			return;
		}

		foreach (string resource : index.m_aMaterials)
		{
			string fileName = FilePath.StripPath(resource);
			string ext;
			string key = FilePath.StripExtension(fileName, ext);
			key.ToLower();

			array<string> list = s_mByName.Get(key);
			if (!list)
			{
				list = {};
				s_mByName.Insert(key, list);
			}

			list.Insert(resource);
		}

		Print("[ACM Thermal] Material index loaded: " + index.m_aMaterials.Count().ToString() + " materials, " + s_mByName.Count().ToString() + " names.", LogLevel.NORMAL);
	}
}
