//
//  ACM
//  ACM Cloth Area Service
//
//  Original ACM-owned script for the modular framework.
//==================================================================================================

/*!
	Area-aware cloth lookup helpers for ACM.

	Vanilla `SCR_CharacterInventoryStorageComponent.GetClothFromArea(typename)`
	resolves the cloth currently worn at a single canonical area. ACM extends
	this with two additional axes:

	  1. Sidecar declarations via `ACM_ClothAreaExtensionComponent` on the cloth
	     prefab (`m_ACM_AdditionalAreaTypes`).
	  2. Area family equivalence via `ACM_AREA_TYPE_ROUTING_MAPPINGS` so that
	     two distinct LoadoutAreaTypes in the same family answer uniformly.

	Use these helpers from ACM-side code (camera fade, hide arrays, reroute
	logic, UI). They do NOT change engine equip acceptance.
*/

class ACM_ClothAreaService
{
	// True when the cloth canonically lives at `targetArea` on `character`,
	// declares the area in its extension list, or declares an area in the same
	// family per the routing registry.
	static bool ClothMatchesArea(IEntity character, IEntity cloth, typename targetArea)
	{
		if (!cloth || targetArea == typename.Empty)
			return false;

		// Canonical match: ask the character's loadout storage. The vanilla
		// engine API keys cloth-by-area on the canonical AreaType only.
		IEntity canonical = GetCanonicalClothAtArea(character, targetArea);
		if (canonical && canonical == cloth)
			return true;

		// Extension match: cloth declares the area in its sidecar list.
		ACM_ClothAreaExtensionComponent ext = ACM_ClothAreaExtensionComponent.Cast(
			cloth.FindComponent(ACM_ClothAreaExtensionComponent));
		if (ext)
		{
			if (ext.ACM_DeclaresArea(targetArea))
				return true;
			if (ext.ACM_DeclaresAreaInFamilyOf(targetArea))
				return true;
		}

		return false;
	}

	// Returns the cloth currently worn at `targetArea`. Falls back to an
	// extension-declared cloth if the canonical slot is empty.
	static IEntity FindClothByArea(IEntity character, typename targetArea)
	{
		if (!character || targetArea == typename.Empty)
			return null;

		IEntity canonical = GetCanonicalClothAtArea(character, targetArea);
		if (canonical)
			return canonical;

		// No canonical match. Walk worn cloth and look for an extension match.
		array<IEntity> wornCloth = {};
		CollectWornCloth(character, wornCloth);

		foreach (IEntity cloth : wornCloth)
		{
			ACM_ClothAreaExtensionComponent ext = ACM_ClothAreaExtensionComponent.Cast(
				cloth.FindComponent(ACM_ClothAreaExtensionComponent));
			if (!ext)
				continue;
			if (ext.ACM_DeclaresArea(targetArea) || ext.ACM_DeclaresAreaInFamilyOf(targetArea))
				return cloth;
		}

		return null;
	}

	// Returns every cloth that satisfies the area (canonical match + all
	// extension matches in the same family). Useful for fade / hide passes
	// that must touch every cloth in a family.
	static void FindAllClothByArea(IEntity character, typename targetArea, out notnull array<IEntity> result)
	{
		result.Clear();
		if (!character || targetArea == typename.Empty)
			return;

		IEntity canonical = GetCanonicalClothAtArea(character, targetArea);
		if (canonical)
			result.Insert(canonical);

		array<IEntity> wornCloth = {};
		CollectWornCloth(character, wornCloth);

		foreach (IEntity cloth : wornCloth)
		{
			if (!cloth || cloth == canonical)
				continue;

			ACM_ClothAreaExtensionComponent ext = ACM_ClothAreaExtensionComponent.Cast(
				cloth.FindComponent(ACM_ClothAreaExtensionComponent));
			if (!ext)
				continue;
			if (ext.ACM_DeclaresArea(targetArea) || ext.ACM_DeclaresAreaInFamilyOf(targetArea))
				result.Insert(cloth);
		}
	}

	// Engine-side canonical lookup. Wraps the loadout storage API so callers
	// don't need to reach for it directly.
	protected static IEntity GetCanonicalClothAtArea(IEntity character, typename targetArea)
	{
		if (!character)
			return null;

		SCR_CharacterInventoryStorageComponent loadout = SCR_CharacterInventoryStorageComponent.Cast(
			character.FindComponent(SCR_CharacterInventoryStorageComponent));
		if (!loadout)
			return null;

		return loadout.GetClothFromArea(targetArea);
	}

	// Walks the character's inventory storages and collects every attached
	// entity that carries a `BaseLoadoutClothComponent`.
	protected static void CollectWornCloth(IEntity character, out notnull array<IEntity> result)
	{
		result.Clear();
		if (!character)
			return;

		SCR_InventoryStorageManagerComponent inv = SCR_InventoryStorageManagerComponent.Cast(
			character.FindComponent(SCR_InventoryStorageManagerComponent));
		if (!inv)
			return;

		array<BaseInventoryStorageComponent> storages = {};
		inv.GetStorages(storages);

		foreach (BaseInventoryStorageComponent storage : storages)
		{
			if (!storage)
				continue;

			int slotCount = storage.GetSlotsCount();
			for (int i = 0; i < slotCount; i++)
			{
				InventoryStorageSlot slot = storage.GetSlot(i);
				if (!slot)
					continue;

				IEntity attached = slot.GetAttachedEntity();
				if (!attached)
					continue;

				BaseLoadoutClothComponent cloth = BaseLoadoutClothComponent.Cast(
					attached.FindComponent(BaseLoadoutClothComponent));
				if (!cloth)
					continue;

				result.Insert(attached);
			}
		}
	}
}

