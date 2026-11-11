//
//  ACM
//  ACM Cloth Area Extension Component
//
//  Original ACM-owned script for the modular framework.
//==================================================================================================

/*!
	Sibling component for cloth prefabs (those carrying `BaseLoadoutClothComponent`).
	Declares additional `LoadoutAreaType`s that the cloth should satisfy for
	ACM-side area queries.

	Why a sibling component instead of `modded class BaseLoadoutClothComponent`:
	the ACM framework only mods script-side `SCR_*` classes; engine-bound
	`Base*` classes are extended via composition. This matches the
	`ACM_PocketStorageComponent` / `ACM_EquipmentStorageComponent` pattern.

	Authoring:
	  1. Add this component to the cloth prefab next to `BaseLoadoutClothComponent`.
	  2. Populate `m_ACM_AdditionalAreaTypes` with one entry per extra area the
	     cloth declares (e.g. an NVG that should answer queries for both
	     `NVS_Slotted` and `ACM_NV_Slotted`).
	  3. Optionally register each declared area in `ACM_AREA_TYPE_ROUTING_MAPPINGS`
	     so family-equivalence queries work uniformly.

	Limitations:
	  - This metadata only affects ACM-side lookups (via `ACM_ClothAreaService`).
	  - Engine equip slot acceptance still matches the cloth's canonical
	    `AreaType` from `BaseLoadoutClothComponent` only.
*/

class ACM_ClothAreaExtensionComponentClass : ScriptComponentClass {}

class ACM_ClothAreaExtensionComponent : ScriptComponent
{
	[Attribute(desc: "Additional LoadoutArea types this cloth declares it satisfies. Add one entry per extra area. Used by ACM_ClothAreaService for area-aware queries (alpha fade, hide arrays, etc.).")]
	protected ref array<ref LoadoutAreaType> m_ACM_AdditionalAreaTypes;

	array<ref LoadoutAreaType> ACM_GetAdditionalAreaTypes()
	{
		return m_ACM_AdditionalAreaTypes;
	}

	// True if the cloth declares the given area in its additional list.
	bool ACM_DeclaresArea(typename area)
	{
		if (area == typename.Empty)
			return false;
		if (!m_ACM_AdditionalAreaTypes || m_ACM_AdditionalAreaTypes.IsEmpty())
			return false;

		foreach (LoadoutAreaType extra : m_ACM_AdditionalAreaTypes)
		{
			if (!extra)
				continue;
			if (extra.Type() == area)
				return true;
		}

		return false;
	}

	// True if any declared additional area shares a family with the target area
	// per the `ACM_AREA_TYPE_ROUTING_MAPPINGS` registry. Use this when callers
	// want uniform treatment of equivalent areas (e.g. all NVG mount slots).
	bool ACM_DeclaresAreaInFamilyOf(typename area)
	{
		if (area == typename.Empty)
			return false;
		if (!m_ACM_AdditionalAreaTypes || m_ACM_AdditionalAreaTypes.IsEmpty())
			return false;

		ACM_EAreaFamily targetFamily = ACM_AreaRoutingTypes.ResolveFamilyForArea(area);
		if (targetFamily == ACM_EAreaFamily.NONE)
			return false;

		foreach (LoadoutAreaType extra : m_ACM_AdditionalAreaTypes)
		{
			if (!extra)
				continue;
			if (ACM_AreaRoutingTypes.ResolveFamilyForArea(extra.Type()) == targetFamily)
				return true;
		}

		return false;
	}
}

