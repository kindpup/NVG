//
//  ACM
//  ACM Item Classification Service
//
//  Original ACM-owned script for the modular framework.
//==================================================================================================

// Logical item family used by placard policy slots to decide whether a slot can accept an item.
// AMMO covers magazine families (RIFLE / MARKSMAN / MG / PISTOL / AK / GL).
// SMOKE / GRENADE / RADIO / PATCH / POUCH / ATAK are utility families and never overlap with AMMO.
// AUTO covers any ECommonItemType placed between `ACM_AUTO_ROUTED_BEGIN` and `ACM_AUTO_ROUTED_END`
// (headsets, NVGs, RailLites, etc.). AUTO-family slots additionally check the slot's specific
// m_ACM_MagCategory against the item's CommonItemType so e.g. a left-ear slot rejects right-ear items.
enum ACM_EItemFamily
{
	NONE,
	AMMO,
	SMOKE,
	GRENADE,
	RADIO,
	PATCH,
	POUCH,
	ATAK,
	AUTO
}

class ACM_ItemClassificationService
{
	// Per-session cache: maps ResourceName -> resolved ACM additional types.
	// Populated on first lookup; cleared automatically when a new session starts
	// because the map lives on the class (static lifetime = session lifetime).
	protected static ref map<string, ref array<ECommonItemType>> s_mResourceAdditionalTypeCache;

	// Per-session cache: maps ResourceName -> the FULL resolved normalized common-type
	// list (override + additional-sidecar + vanilla CommonItemType). This is the hot path
	// hit per-item/per-slot by inventory UI, routing, and first-person hide; without it every
	// classification call re-runs Resource.Load(). Only populated on a fully-read prefab source
	// so a transient (JIP / early-init) failure never poisons classification for the session.
	protected static ref map<string, ref array<ECommonItemType>> s_mResourceCommonTypeCache;

	// Per-session cache: maps "ResourceName|ComponentTypeName" -> whether the prefab source
	// declares that component. ResourceHasComponent() is called repeatedly from the family
	// resolver (radio / magazine / explosive checks) and each miss did a Resource.Load().
	protected static ref map<string, bool> s_mResourceComponentCache;

	protected static void ACM_EnsureResourceTypeCache()
	{
		if (!s_mResourceAdditionalTypeCache)
			s_mResourceAdditionalTypeCache = new map<string, ref array<ECommonItemType>>();
		if (!s_mResourceCommonTypeCache)
			s_mResourceCommonTypeCache = new map<string, ref array<ECommonItemType>>();
		if (!s_mResourceComponentCache)
			s_mResourceComponentCache = new map<string, bool>();
	}

	// Ammo-magazine families. Anything in this list is a magazine cell category.
	static bool ACM_IsAmmoCategory(ECommonItemType type)
	{
		ECommonItemType normalized = NormalizeCommonItemType(type);
		return normalized == ECommonItemType.ACM_RIFLE_AMMO
			|| normalized == ECommonItemType.ACM_MARKSMAN_AMMO
			|| normalized == ECommonItemType.ACM_MG_AMMO
			|| normalized == ECommonItemType.ACM_PISTOL_AMMO
			|| normalized == ECommonItemType.ACM_AK_AMMO
			|| normalized == ECommonItemType.ACM_GL_AMMO;
	}

	static bool ACM_IsSmokeCategory(ECommonItemType type)
	{
		return NormalizeCommonItemType(type) == ECommonItemType.ACM_SMOKE_GRENADE;
	}

	static bool ACM_IsGrenadeCategory(ECommonItemType type)
	{
		return NormalizeCommonItemType(type) == ECommonItemType.ACM_GRENADE;
	}

	static bool ACM_IsRadioCategory(ECommonItemType type)
	{
		return NormalizeCommonItemType(type) == ECommonItemType.ACM_RADIO;
	}

	static bool ACM_IsPatchCategory(ECommonItemType type)
	{
		return NormalizeCommonItemType(type) == ECommonItemType.ACM_PATCH;
	}

	static bool ACM_IsPouchCategory(ECommonItemType type)
	{
		return NormalizeCommonItemType(type) == ECommonItemType.ACM_Pouch;
	}

	static bool ACM_IsAtakCategory(ECommonItemType type)
	{
		return NormalizeCommonItemType(type) == ECommonItemType.ACM_ATAK;
	}

	// Returns true if the type sits between `ACM_AUTO_ROUTED_BEGIN` and `ACM_AUTO_ROUTED_END`
	// in the modded ECommonItemType enum. Single source of truth for auto-routing + FP-hide.
	static bool ACM_IsAutoRoutedType(ECommonItemType type)
	{
		ECommonItemType normalized = NormalizeCommonItemType(type);
		return normalized > ECommonItemType.ACM_AUTO_ROUTED_BEGIN
			&& normalized < ECommonItemType.ACM_AUTO_ROUTED_END;
	}

	// Returns true if any of the item's CommonItemType / m_ACM_AdditionalCommonItemTypes entries
	// is in the auto-routed sentinel range.
	static bool ACM_ItemHasAutoRoutedType(IEntity item)
	{
		if (!item)
			return false;

		array<ECommonItemType> additional = {};
		ACM_GetItemAdditionalCommonTypes(item, additional);
		foreach (ECommonItemType t : additional)
		{
			if (ACM_IsAutoRoutedType(t))
				return true;
		}

		array<ECommonItemType> commonTypes = {};
		CollectNormalizedCommonTypes(item, commonTypes);
		foreach (ECommonItemType t : commonTypes)
		{
			if (ACM_IsAutoRoutedType(t))
				return true;
		}
		return false;
	}

	// Resource-prefab parallel of ACM_ItemHasAutoRoutedType for engine resource-based paths.
	static bool ACM_ResourceHasAutoRoutedType(ResourceName resourceName)
	{
		if (resourceName.IsEmpty())
			return false;

		array<ECommonItemType> additional = {};
		ACM_GetResourceAdditionalCommonTypes(resourceName, additional);
		foreach (ECommonItemType t : additional)
		{
			if (ACM_IsAutoRoutedType(t))
				return true;
		}

		array<ECommonItemType> commonTypes = {};
		CollectNormalizedResourceCommonTypes(resourceName, commonTypes);
		foreach (ECommonItemType t : commonTypes)
		{
			if (ACM_IsAutoRoutedType(t))
				return true;
		}
		return false;
	}

	// Maps a slot's configured common-item category to its logical family.
	static ACM_EItemFamily ACM_SlotCategoryToFamily(ECommonItemType type)
	{
		if (ACM_IsAmmoCategory(type))
			return ACM_EItemFamily.AMMO;
		if (ACM_IsSmokeCategory(type))
			return ACM_EItemFamily.SMOKE;
		if (ACM_IsGrenadeCategory(type))
			return ACM_EItemFamily.GRENADE;
		if (ACM_IsRadioCategory(type))
			return ACM_EItemFamily.RADIO;
		if (ACM_IsPatchCategory(type))
			return ACM_EItemFamily.PATCH;
		if (ACM_IsPouchCategory(type))
			return ACM_EItemFamily.POUCH;
		if (ACM_IsAtakCategory(type))
			return ACM_EItemFamily.ATAK;
		if (ACM_IsAutoRoutedType(type))
			return ACM_EItemFamily.AUTO;
		return ACM_EItemFamily.NONE;
	}

	// Returns true if the item's prefab path or name matches a smoke-style identifier.
	// Used as a final fallback for content that lacks a CommonItemType, weapon-type tag, and
	// ACM additional-type sidecar entry.
	static bool ACM_IsSmokePrefab(IEntity item)
	{
		if (!item)
			return false;
		EntityPrefabData prefabData = item.GetPrefabData();
		if (!prefabData)
			return false;
		return ACM_IsSmokeResourceName(prefabData.GetPrefabName());
	}

	static bool ACM_IsSmokeResourceName(ResourceName resourceName)
	{
		if (resourceName.IsEmpty())
			return false;
		string lower = resourceName;
		lower.ToLower();
		return lower.Contains("smoke");
	}

	// Returns true if the item's prefab path or name matches a frag/throwable-grenade identifier.
	static bool ACM_IsGrenadePrefab(IEntity item)
	{
		if (!item)
			return false;
		EntityPrefabData prefabData = item.GetPrefabData();
		if (!prefabData)
			return false;
		return ACM_IsGrenadeResourceName(prefabData.GetPrefabName());
	}

	static bool ACM_IsGrenadeResourceName(ResourceName resourceName)
	{
		if (resourceName.IsEmpty())
			return false;
		string lower = resourceName;
		lower.ToLower();
		// "smoke" is filtered upstream; here we only want frag/throwable identifiers.
		if (lower.Contains("smoke"))
			return false;
		return lower.Contains("grenade") || lower.Contains("frag") || lower.Contains("throwable");
	}

	// Toggle to log live item family resolution. Leave false in shipping builds.
	static const bool ACM_DEBUG_FAMILY = false;

	// =========================================================================================
	// ACM Additional Common Item Types — primary classification source.
	// These helpers read ONLY the m_ACM_AdditionalCommonItemTypes sidecar list (prefab + live)
	// and do not consult the vanilla CommonItemType. They are the highest-priority source the
	// family resolver consults so vanilla type can never override an ACM-authored intent.
	// =========================================================================================

	// Collects every ACM additional common-item type entry on a live item, normalized.
	static void ACM_GetItemAdditionalCommonTypes(IEntity item, out array<ECommonItemType> resolvedTypes)
	{
		resolvedTypes = {};
		if (!item)
			return;

		EntityPrefabData prefabData = item.GetPrefabData();
		if (prefabData)
			ACM_GetResourceAdditionalCommonTypes(prefabData.GetPrefabName(), resolvedTypes);

		ItemAttributeCollection attributes = GetItemAttributes(item);
		if (!attributes)
			return;

		SCR_ItemAttributeCollection ACMAttributes = SCR_ItemAttributeCollection.Cast(attributes);
		if (ACMAttributes)
			AppendNormalizedTypeNames(ACMAttributes.ACM_GetAdditionalCommonItemTypeNames(), resolvedTypes);
	}

	// Collects every ACM additional common-item type entry on a prefab/resource, normalized.
	// Results are cached by ResourceName so Resource.Load() is only called once per prefab
	// per session, eliminating repeated disk I/O during inventory drag/hover hot paths.
	static void ACM_GetResourceAdditionalCommonTypes(ResourceName resourceName, out array<ECommonItemType> resolvedTypes)
	{
		if (!resolvedTypes)
			resolvedTypes = {};
		if (resourceName.IsEmpty())
			return;

		ACM_EnsureResourceTypeCache();

		string cacheKey = resourceName;
		array<ECommonItemType> cached;
		if (s_mResourceAdditionalTypeCache.Find(cacheKey, cached) && cached)
		{
			foreach (ECommonItemType t : cached)
				AppendNormalizedType(resolvedTypes, t);
			return;
		}

		array<ECommonItemType> fresh = {};
		bool resolvedSuccessfully = false;

		Resource entityResource = Resource.Load(resourceName);
		if (entityResource && entityResource.IsValid())
		{
			BaseResourceObject resourceObject = entityResource.GetResource();
			if (resourceObject)
			{
				IEntitySource entitySource = resourceObject.ToEntitySource();
				if (entitySource)
				{
					// Only a fully readable prefab source counts as a definitive answer.
					resolvedSuccessfully = true;

					for (int i = 0, componentCount = entitySource.GetComponentCount(); i < componentCount; i++)
					{
						IEntityComponentSource componentSource = entitySource.GetComponent(i);
						if (!componentSource)
							continue;

						typename sourceType = componentSource.GetClassName().ToType();
						if (!sourceType)
							continue;

						if (!sourceType.IsInherited(InventoryItemComponent) && !sourceType.IsInherited(BaseInventoryStorageComponent))
							continue;

						BaseContainer attributes = componentSource.GetObject("Attributes");
						if (!attributes)
							continue;

						array<string> additionalTypeNames = {};
						if (!attributes.Get("m_ACM_AdditionalCommonItemTypes", additionalTypeNames))
							continue;

						AppendNormalizedTypeNames(additionalTypeNames, fresh);
					}
				}
			}
		}

		// REPLICATION-SAFETY: never cache a failed load. Poisoning the cache with an
		// empty list on a transient failure (JIP streaming, early init) makes THIS
		// machine permanently classify the prefab as untyped while the server and
		// other clients classify it correctly — items then route/validate/render
		// differently per machine for the rest of the session.
		if (resolvedSuccessfully)
			s_mResourceAdditionalTypeCache.Set(cacheKey, fresh);

		foreach (ECommonItemType t : fresh)
			AppendNormalizedType(resolvedTypes, t);
	}

	// True when the ACM additional list contains the requested type.
	static bool ACM_ItemHasAdditionalCommonType(IEntity item, ECommonItemType type)
	{
		ECommonItemType normalized = NormalizeCommonItemType(type);
		if (normalized == ECommonItemType.NONE || normalized == -1)
			return false;

		array<ECommonItemType> additional = {};
		ACM_GetItemAdditionalCommonTypes(item, additional);
		return additional.Find(normalized) != -1;
	}

	static bool ACM_ResourceHasAdditionalCommonType(ResourceName resourceName, ECommonItemType type)
	{
		ECommonItemType normalized = NormalizeCommonItemType(type);
		if (normalized == ECommonItemType.NONE || normalized == -1)
			return false;

		array<ECommonItemType> additional = {};
		ACM_GetResourceAdditionalCommonTypes(resourceName, additional);
		return additional.Find(normalized) != -1;
	}

	// Returns the first ACM-recognized type from the additional list. Used by the family resolver
	// to make additional-type intent strictly authoritative over vanilla CommonItemType.
	static bool ACM_ItemHasAnyACMAdditionalType(IEntity item, out ECommonItemType matchedType)
	{
		matchedType = ECommonItemType.NONE;
		array<ECommonItemType> additional = {};
		ACM_GetItemAdditionalCommonTypes(item, additional);
		return ACM_PickFirstACMType(additional, matchedType);
	}

	static bool ACM_ResourceHasAnyACMAdditionalType(ResourceName resourceName, out ECommonItemType matchedType)
	{
		matchedType = ECommonItemType.NONE;
		array<ECommonItemType> additional = {};
		ACM_GetResourceAdditionalCommonTypes(resourceName, additional);
		return ACM_PickFirstACMType(additional, matchedType);
	}

	// Selects the first ACM-authored type whose family resolves cleanly.
	// This is deliberately broader than just ammo/radio/grenade: if an item has
	// ACM additional routing/visibility metadata, that metadata must remain
	// authoritative over a vanilla CommonItemType during BLE/arsenal replay.
	protected static bool ACM_PickFirstACMType(array<ECommonItemType> candidates, out ECommonItemType matchedType)
	{
		matchedType = ECommonItemType.NONE;
		if (!candidates)
			return false;

		foreach (ECommonItemType candidate : candidates)
		{
			if (ACM_IsAmmoCategory(candidate)
				|| ACM_IsSmokeCategory(candidate)
				|| ACM_IsGrenadeCategory(candidate)
				|| ACM_IsRadioCategory(candidate)
				|| ACM_IsPatchCategory(candidate)
				|| ACM_IsPouchCategory(candidate)
				|| ACM_IsAtakCategory(candidate)
				|| ACM_IsAutoRoutedType(candidate))
			{
				matchedType = candidate;
				return true;
			}
		}
		return false;
	}

	protected static ACM_EItemFamily ACM_AdditionalTypeToFamily(ECommonItemType type)
	{
		if (ACM_IsRadioCategory(type))
			return ACM_EItemFamily.RADIO;
		if (ACM_IsSmokeCategory(type))
			return ACM_EItemFamily.SMOKE;
		if (ACM_IsGrenadeCategory(type))
			return ACM_EItemFamily.GRENADE;
		if (ACM_IsPatchCategory(type))
			return ACM_EItemFamily.PATCH;
		if (ACM_IsPouchCategory(type))
			return ACM_EItemFamily.POUCH;
		if (ACM_IsAtakCategory(type))
			return ACM_EItemFamily.ATAK;
		if (ACM_IsAutoRoutedType(type))
			return ACM_EItemFamily.AUTO;
		if (ACM_IsAmmoCategory(type))
			return ACM_EItemFamily.AMMO;
		return ACM_EItemFamily.NONE;
	}

	// Resolves a live item to its logical family. ACM Additional Common Item Types are the
	// authoritative source — if the item declares any ACM routing type in its additional list,
	// that wins over vanilla CommonItemType, weapon-type tags, and component fallbacks.
	static ACM_EItemFamily ACM_ResolveItemFamily(IEntity item)
	{
		ACM_EItemFamily resolved = ACM_ResolveItemFamilyInternal(item);
		if (ACM_DEBUG_FAMILY)
			DebugLogResolveItemFamily(item, resolved);
		return resolved;
	}

	protected static ACM_EItemFamily ACM_ResolveItemFamilyInternal(IEntity item)
	{
		if (!item)
			return ACM_EItemFamily.NONE;

		// Step 1: ACM Additional Common Item Types win over everything else.
		ECommonItemType additionalMatch;
		if (ACM_ItemHasAnyACMAdditionalType(item, additionalMatch))
		{
			ACM_EItemFamily fromAdditional = ACM_AdditionalTypeToFamily(additionalMatch);
			if (fromAdditional != ACM_EItemFamily.NONE)
				return fromAdditional;
		}

		// Step 2: vanilla CommonItemType — but only consult specific utility categories first so a
		// stray vanilla AMMO classification on a smoke/frag/radio prefab cannot win.
		if (IsRadio(item))
			return ACM_EItemFamily.RADIO;
		if (ItemMatchesCommonType(item, ECommonItemType.ACM_RADIO))
			return ACM_EItemFamily.RADIO;

		if (ItemMatchesCommonType(item, ECommonItemType.ACM_SMOKE_GRENADE)
			|| MatchesWeaponType(item, EWeaponType.WT_SMOKEGRENADE)
			|| ACM_IsSmokePrefab(item))
			return ACM_EItemFamily.SMOKE;

		if (ItemMatchesCommonType(item, ECommonItemType.ACM_GRENADE)
			|| MatchesWeaponType(item, EWeaponType.WT_FRAGGRENADE)
			|| IsExplosiveCharge(item)
			|| ACM_IsGrenadePrefab(item))
			return ACM_EItemFamily.GRENADE;

		if (ItemMatchesCommonType(item, ECommonItemType.ACM_PATCH))
			return ACM_EItemFamily.PATCH;

		if (ItemMatchesCommonType(item, ECommonItemType.ACM_Pouch))
			return ACM_EItemFamily.POUCH;

		if (ItemMatchesCommonType(item, ECommonItemType.ACM_ATAK))
			return ACM_EItemFamily.ATAK;

		if (ACM_ItemHasAutoRoutedType(item))
			return ACM_EItemFamily.AUTO;

		// Step 3: component fallback. Magazine identity is intentionally last so a smoke/frag/radio
		// item that somehow carries a magazine component cannot be misclassified as ammo.
		if (IsMagazine(item))
			return ACM_EItemFamily.AMMO;

		return ACM_EItemFamily.NONE;
	}

	protected static void DebugLogResolveItemFamily(IEntity item, ACM_EItemFamily resolved)
	{
		if (!item)
		{
			Print("[ACM Family] item=null family=NONE", LogLevel.NORMAL);
			return;
		}

		string prefab = "<no-prefab>";
		EntityPrefabData prefabData = item.GetPrefabData();
		if (prefabData)
			prefab = prefabData.GetPrefabName();

		array<ECommonItemType> commonTypes = {};
		GetNormalizedCommonTypes(item, commonTypes);
		string commonTypesStr = "[";
		for (int i = 0; i < commonTypes.Count(); i++)
		{
			if (i > 0)
				commonTypesStr += ",";
			commonTypesStr += commonTypes[i].ToString();
		}
		commonTypesStr += "]";

		array<ECommonItemType> additionalTypes = {};
		ACM_GetItemAdditionalCommonTypes(item, additionalTypes);
		string additionalStr = "[";
		for (int i = 0; i < additionalTypes.Count(); i++)
		{
			if (i > 0)
				additionalStr += ",";
			additionalStr += additionalTypes[i].ToString();
		}
		additionalStr += "]";

		bool hasMag = IsMagazine(item);
		bool hasExplosive = IsExplosiveCharge(item);
		bool hasRadio = IsRadio(item);

		EWeaponType weaponType;
		string weaponTypeStr = "<none>";
		if (TryGetWeaponType(item, weaponType))
			weaponTypeStr = weaponType.ToString();

		Print(string.Format("[ACM Family] item=%1 additional=%2 commonTypes=%3 mag=%4 explosive=%5 radio=%6 weapon=%7 family=%8",
			prefab, additionalStr, commonTypesStr, hasMag, hasExplosive, hasRadio, weaponTypeStr, resolved), LogLevel.NORMAL);
	}

	// Public helper to explicitly dump an item's normalized common types and ACM additional types.
	// Callable from other scripts for debugging purposes.
	protected static void DebugLogItemTypes(IEntity item, string tag = "ACM")
	{
		if (!item)
		{
			Print(string.Format("[%1] item=null", tag), LogLevel.NORMAL);
			return;
		}

		string prefab = "<no-prefab>";
		EntityPrefabData prefabData = item.GetPrefabData();
		if (prefabData)
			prefab = prefabData.GetPrefabName();

		array<ECommonItemType> commonTypes = {};
		GetNormalizedCommonTypes(item, commonTypes);
		string commonTypesStr = "[";
		for (int i = 0; i < commonTypes.Count(); i++)
		{
			if (i > 0)
				commonTypesStr += ",";
			commonTypesStr += commonTypes[i].ToString();
		}
		commonTypesStr += "]";

		array<ECommonItemType> additionalTypes = {};
		ACM_GetItemAdditionalCommonTypes(item, additionalTypes);
		string additionalStr = "[";
		for (int j = 0; j < additionalTypes.Count(); j++)
		{
			if (j > 0)
				additionalStr += ",";
			additionalStr += additionalTypes[j].ToString();
		}
		additionalStr += "]";

		Print(string.Format("[%1] prefab=%2 additional=%3 commonTypes=%4", tag, prefab, additionalStr, commonTypesStr), LogLevel.NORMAL);
	}

	// Resource-prefab parallel to ACM_ResolveItemFamily for the engine's resource-based insert path.
	// Same authoritative order: ACM additional list first, then vanilla type / components.
	static ACM_EItemFamily ACM_ResolveResourceFamily(ResourceName resourceName)
	{
		if (resourceName.IsEmpty())
			return ACM_EItemFamily.NONE;

		// Step 1: ACM Additional Common Item Types.
		ECommonItemType additionalMatch;
		if (ACM_ResourceHasAnyACMAdditionalType(resourceName, additionalMatch))
		{
			ACM_EItemFamily fromAdditional = ACM_AdditionalTypeToFamily(additionalMatch);
			if (fromAdditional != ACM_EItemFamily.NONE)
				return fromAdditional;
		}

		// Step 2: vanilla CommonItemType + component / prefab fallbacks.
		if (ResourceHasComponent(resourceName, SCR_RadioComponent)
			|| ResourceMatchesCommonType(resourceName, ECommonItemType.ACM_RADIO))
			return ACM_EItemFamily.RADIO;

		if (ResourceMatchesCommonType(resourceName, ECommonItemType.ACM_SMOKE_GRENADE)
			|| ACM_IsSmokeResourceName(resourceName))
			return ACM_EItemFamily.SMOKE;

		if (ResourceMatchesCommonType(resourceName, ECommonItemType.ACM_GRENADE)
			|| ResourceHasComponent(resourceName, SCR_ExplosiveChargeInventoryItemComponent)
			|| ACM_IsGrenadeResourceName(resourceName))
			return ACM_EItemFamily.GRENADE;

		if (ResourceMatchesCommonType(resourceName, ECommonItemType.ACM_PATCH))
			return ACM_EItemFamily.PATCH;

		if (ResourceMatchesCommonType(resourceName, ECommonItemType.ACM_Pouch))
			return ACM_EItemFamily.POUCH;

		if (ResourceMatchesCommonType(resourceName, ECommonItemType.ACM_ATAK))
			return ACM_EItemFamily.ATAK;

		if (ACM_ResourceHasAutoRoutedType(resourceName))
			return ACM_EItemFamily.AUTO;

		bool isMagazine = ResourceHasComponent(resourceName, InventoryMagazineComponent)
			|| ResourceHasComponent(resourceName, MagazineComponent);
		if (isMagazine)
			return ACM_EItemFamily.AMMO;

		return ACM_EItemFamily.NONE;
	}


	protected static bool TryParseNormalizedCommonItemTypeName(string rawTypeName, out ECommonItemType resolvedType)
	{
		resolvedType = ECommonItemType.NONE;

		if (rawTypeName.IsEmpty() || rawTypeName == ECommonItemType.NONE.ToString())
			return false;

		string normalizedTypeName = NormalizeCommonItemTypeName(rawTypeName);
		int parsedType = typename.StringToEnum(ECommonItemType, normalizedTypeName);
		if (parsedType < 0)
			return false;

		resolvedType = parsedType;
		return resolvedType != ECommonItemType.NONE && resolvedType != -1;
	}

	protected static void AppendNormalizedType(inout array<ECommonItemType> resolvedTypes, ECommonItemType itemType)
	{
		if (itemType == -1 || itemType == ECommonItemType.NONE)
			return;

		ECommonItemType normalizedType = NormalizeCommonItemType(itemType);
		if (normalizedType == -1 || normalizedType == ECommonItemType.NONE)
			return;

		if (resolvedTypes.Find(normalizedType) == -1)
			resolvedTypes.Insert(normalizedType);
	}

	protected static void AppendNormalizedTypeName(inout array<ECommonItemType> resolvedTypes, string rawTypeName)
	{
		ECommonItemType parsedType;
		if (!TryParseNormalizedCommonItemTypeName(rawTypeName, parsedType))
			return;

		AppendNormalizedType(resolvedTypes, parsedType);
	}

	protected static void AppendNormalizedTypeNames(array<string> rawTypeNames, inout array<ECommonItemType> resolvedTypes)
	{
		if (!rawTypeNames)
			return;

		foreach (string rawTypeName : rawTypeNames)
		{
			AppendNormalizedTypeName(resolvedTypes, rawTypeName);
		}
	}

	static string NormalizeCommonItemTypeName(string typeName)
	{
		if (typeName.IsEmpty())
			return typeName;

		typeName = ACM_ExternalCompatibilityAliases.NormalizeExternalItemTypeName(typeName);

		switch (typeName)
		{
			case "RHS_RIFLE_AMMO":
			case "ARMA_RIFLE_AMMO":
			case "GRS_RIFLE_AMMO":
			case "ACM_RIFLE_AMMO":
				return "ACM_RIFLE_AMMO";

			case "RHS_MARKSMAN_AMMO":
			case "ARMA_MARKSMAN_AMMO":
			case "GRS_MARKSMAN_AMMO":
			case "ACM_MARKSMAN_AMMO":
				return "ACM_MARKSMAN_AMMO";

			case "RHS_MG_AMMO":
			case "ARMA_MG_AMMO":
			case "GRS_MG_AMMO":
			case "ACM_MG_AMMO":
				return "ACM_MG_AMMO";

			case "RHS_PISTOL_AMMO":
			case "ARMA_PISTOL_AMMO":
			case "GRS_PISTOL_AMMO":
			case "ACM_PISTOL_AMMO":
				return "ACM_PISTOL_AMMO";

			case "RHS_SMOKE_GRENADE":
			case "ARMA_SMOKE_GRENADE":
			case "GRS_SMOKE_GRENADE":
			case "ACM_SMOKE_GRENADE":
				return "ACM_SMOKE_GRENADE";

			case "RHS_FRAG_GRENADE":
			case "ARMA_GRENADE":
			case "GRS_FRAG_GRENADE":
			case "GRS_GRENADE":
			case "ACM_GRENADE":
				return "ACM_GRENADE";

			case "RADIO":
			case "ARMA_RADIO":
			case "GRS_RADIO":
			case "ACM_RADIO":
				return "ACM_RADIO";

			case "GRS_PATCH":
			case "ACM_PATCH":
				return "ACM_PATCH";

			case "GRS_ATAK":
			case "ACM_ATAK":
				return "ACM_ATAK";
		}

		return typeName;
	}

	static ECommonItemType NormalizeCommonItemType(ECommonItemType itemType)
	{
		if (itemType == -1)
			return itemType;

		ECommonItemType normalizedType;
		if (!TryParseNormalizedCommonItemTypeName(itemType.ToString(), normalizedType))
			return itemType;

		return normalizedType;
	}

	static void GetNormalizedCommonTypes(IEntity item, out array<ECommonItemType> resolvedTypes)
	{
		resolvedTypes = {};
		CollectNormalizedCommonTypes(item, resolvedTypes);
	}

	static void GetNormalizedResourceCommonTypes(ResourceName resourceName, out array<ECommonItemType> resolvedTypes)
	{
		resolvedTypes = {};
		CollectNormalizedResourceCommonTypes(resourceName, resolvedTypes);
	}

	static ECommonItemType GetNormalizedCommonType(IEntity item)
	{
		array<ECommonItemType> resolvedTypes = {};
		GetNormalizedCommonTypes(item, resolvedTypes);
		if (!resolvedTypes.IsEmpty())
			return resolvedTypes[0];

		return ECommonItemType.NONE;
	}

	static ECommonItemType GetNormalizedResourceCommonType(ResourceName resourceName)
	{
		array<ECommonItemType> resolvedTypes = {};
		GetNormalizedResourceCommonTypes(resourceName, resolvedTypes);
		if (!resolvedTypes.IsEmpty())
			return resolvedTypes[0];

		return ECommonItemType.NONE;
	}

	static bool ItemMatchesCommonType(IEntity item, ECommonItemType expectedType)
	{
		ECommonItemType normalizedExpectedType = NormalizeCommonItemType(expectedType);
		if (normalizedExpectedType == -1 || normalizedExpectedType == ECommonItemType.NONE)
			return false;

		// ACM Additional Common Item Types are authoritative: if the item declares any ACM
		// routable type in its sidecar list, the match is decided strictly from that list and
		// the vanilla CommonItemType cannot override the answer.
		array<ECommonItemType> additional = {};
		ACM_GetItemAdditionalCommonTypes(item, additional);
		ECommonItemType pickedAdditional;
		if (ACM_PickFirstACMType(additional, pickedAdditional))
			return additional.Find(normalizedExpectedType) != -1;

		array<ECommonItemType> resolvedTypes = {};
		CollectNormalizedCommonTypes(item, resolvedTypes);
		return resolvedTypes.Find(normalizedExpectedType) != -1;
	}

	static bool ResourceMatchesCommonType(ResourceName resourceName, ECommonItemType expectedType)
	{
		ECommonItemType normalizedExpectedType = NormalizeCommonItemType(expectedType);
		if (normalizedExpectedType == -1 || normalizedExpectedType == ECommonItemType.NONE)
			return false;

		// Same authoritative-additional rule for the prefab/resource path.
		array<ECommonItemType> additional = {};
		ACM_GetResourceAdditionalCommonTypes(resourceName, additional);
		ECommonItemType pickedAdditional;
		if (ACM_PickFirstACMType(additional, pickedAdditional))
			return additional.Find(normalizedExpectedType) != -1;

		array<ECommonItemType> resolvedTypes = {};
		CollectNormalizedResourceCommonTypes(resourceName, resolvedTypes);
		return resolvedTypes.Find(normalizedExpectedType) != -1;
	}

	protected static bool TryGetNormalizedOverrideType(IEntity item, out ECommonItemType resolvedType)
	{
		resolvedType = ECommonItemType.NONE;

		if (!item)
			return false;

		ACM_ItemTypeOverrideComponent overrideComponent = ACM_ItemTypeOverrideComponent.Cast(item.FindComponent(ACM_ItemTypeOverrideComponent));
		if (!overrideComponent)
			return false;

		return overrideComponent.ACM_TryGetNormalizedType(resolvedType);
	}

	protected static bool TryGetNormalizedOverrideType(IEntitySource entitySource, out ECommonItemType resolvedType)
	{
		resolvedType = ECommonItemType.NONE;

		if (!entitySource)
			return false;

		for (int i = 0, componentCount = entitySource.GetComponentCount(); i < componentCount; i++)
		{
			IEntityComponentSource componentSource = entitySource.GetComponent(i);
			if (!componentSource)
				continue;

			typename sourceType = componentSource.GetClassName().ToType();
			if (!sourceType || !sourceType.IsInherited(ACM_ItemTypeOverrideComponent))
				continue;

			BaseContainer overrideData = componentSource.GetObject("m_ACM_TypeOverride");
			if (!overrideData)
				continue;

			string commonTypeName;
			if (overrideData.Get("CommonType", commonTypeName))
			{
				string normalizedTypeName = NormalizeCommonItemTypeName(commonTypeName);
				int parsedType = typename.StringToEnum(ECommonItemType, normalizedTypeName);
				if (parsedType >= 0)
				{
					resolvedType = parsedType;
					if (resolvedType != ECommonItemType.NONE && resolvedType != -1)
						return true;
				}
			}

			string secondaryTypeName;
			if (!overrideData.Get("SecondaryCommonType", secondaryTypeName))
				continue;

			if (secondaryTypeName.IsEmpty() || secondaryTypeName == ECommonItemType.NONE.ToString())
				continue;

			string normalizedSecondaryTypeName = NormalizeCommonItemTypeName(secondaryTypeName);
			int parsedSecondaryType = typename.StringToEnum(ECommonItemType, normalizedSecondaryTypeName);
			if (parsedSecondaryType < 0)
				continue;

			resolvedType = parsedSecondaryType;
			return resolvedType != ECommonItemType.NONE && resolvedType != -1;
		}

		return false;
	}

	static bool ResourceHasComponent(ResourceName resourceName, typename componentType)
	{
		if (!componentType || resourceName.IsEmpty())
			return false;

		ACM_EnsureResourceTypeCache();

		string resKey = resourceName;
		string cacheKey = resKey + "|" + componentType.ToString();
		bool cachedResult;
		if (s_mResourceComponentCache.Find(cacheKey, cachedResult))
			return cachedResult;

		Resource entityResource = Resource.Load(resourceName);
		if (!entityResource.IsValid())
			return false;

		BaseResourceObject resourceObject = entityResource.GetResource();
		if (!resourceObject)
			return false;

		IEntitySource entitySource = resourceObject.ToEntitySource();
		if (!entitySource)
			return false;

		// Only reached on a fully-read prefab source, so caching the result never
		// poisons classification with a transient (JIP / early-init) load failure.
		bool found = false;
		for (int i = 0, componentCount = entitySource.GetComponentCount(); i < componentCount; i++)
		{
			IEntityComponentSource componentSource = entitySource.GetComponent(i);
			if (!componentSource)
				continue;

			typename sourceType = componentSource.GetClassName().ToType();
			if (sourceType && sourceType.IsInherited(componentType))
			{
				found = true;
				break;
			}
		}

		s_mResourceComponentCache.Set(cacheKey, found);
		return found;
	}

	static bool IsMagazine(IEntity item)
	{
		if (!item)
			return false;

		return InventoryMagazineComponent.Cast(item.FindComponent(InventoryMagazineComponent)) != null
			|| MagazineComponent.Cast(item.FindComponent(MagazineComponent)) != null;
	}

	static bool IsExplosiveCharge(IEntity item)
	{
		if (!item)
			return false;

		return SCR_ExplosiveChargeInventoryItemComponent.Cast(item.FindComponent(SCR_ExplosiveChargeInventoryItemComponent)) != null;
	}

	static bool IsHandgun(IEntity item)
	{
		return MatchesWeaponType(item, EWeaponType.WT_HANDGUN);
	}

	static bool IsRifle(IEntity item)
	{
		EWeaponType resolvedWeaponType;
		if (!TryGetWeaponType(item, resolvedWeaponType))
			return false;

		return IsRifleFamilyWeaponType(resolvedWeaponType);
	}

	static bool IsHolsterSidearm(IEntity item)
	{
		if (!MatchesWeaponType(item, EWeaponType.WT_HANDGUN))
			return false;

		EntityPrefabData prefabData = item.GetPrefabData();
		if (!prefabData)
			return true;

		string prefabName = prefabData.GetPrefabName();
		if (prefabName.IsEmpty())
			return true;

		if (prefabName.Contains("Flare") || prefabName.Contains("flare") || prefabName.Contains("Signal") || prefabName.Contains("signal"))
			return false;

		return true;
	}

	static bool MatchesWeaponType(IEntity item, EWeaponType expectedWeaponType)
	{
		EWeaponType resolvedWeaponType;
		if (!TryGetWeaponType(item, resolvedWeaponType))
			return false;

		if (expectedWeaponType == EWeaponType.WT_RIFLE)
			return IsRifleFamilyWeaponType(resolvedWeaponType);

		return resolvedWeaponType == expectedWeaponType;
	}

	static bool IsRadio(IEntity item)
	{
		if (!item)
			return false;

		return SCR_RadioComponent.Cast(item.FindComponent(SCR_RadioComponent)) != null;
	}

	static bool IsPatch(IEntity item)
	{
		return ItemMatchesCommonType(item, ECommonItemType.ACM_PATCH);
	}

	static bool IsAccessoryItem(IEntity item)
	{
		if (!item)
			return false;

		return InventoryItemComponent.Cast(item.FindComponent(InventoryItemComponent)) != null
			|| BaseLoadoutClothComponent.Cast(item.FindComponent(BaseLoadoutClothComponent)) != null;
	}

	static bool IsChestAccessory(IEntity item)
	{
		if (!item)
			return false;

		if (ItemMatchesCommonType(item, ECommonItemType.ACM_ATAK))
			return true;

		EntityPrefabData prefabData = item.GetPrefabData();
		if (!prefabData)
			return false;

		return IsChestAccessoryResource(prefabData.GetPrefabName());
	}

	static bool IsChestAccessoryResource(ResourceName resourceName)
	{
		if (resourceName.IsEmpty())
			return false;

		if (ResourceMatchesCommonType(resourceName, ECommonItemType.ACM_ATAK))
			return true;

		return resourceName.Contains("Prefabs/ATAK/") || resourceName.Contains("Prefabs/Kagwerks/");
	}

	protected static ItemAttributeCollection GetItemAttributes(IEntity item)
	{
		if (!item)
			return null;

		InventoryItemComponent itemComponent = InventoryItemComponent.Cast(item.FindComponent(InventoryItemComponent));
		if (itemComponent)
		{
			ItemAttributeCollection attributes = itemComponent.GetAttributes();
			if (attributes)
				return attributes;
		}

		BaseInventoryStorageComponent storageComponent = BaseInventoryStorageComponent.Cast(item.FindComponent(BaseInventoryStorageComponent));
		if (storageComponent)
			return storageComponent.GetAttributes();

		return null;
	}

	protected static void CollectNormalizedCommonTypes(IEntity item, inout array<ECommonItemType> resolvedTypes)
	{
		if (!item)
			return;

		ECommonItemType overrideType;
		if (TryGetNormalizedOverrideType(item, overrideType))
			AppendNormalizedType(resolvedTypes, overrideType);

		EntityPrefabData prefabData = item.GetPrefabData();
		if (prefabData)
			CollectNormalizedResourceCommonTypes(prefabData.GetPrefabName(), resolvedTypes);

		ItemAttributeCollection attributes = GetItemAttributes(item);
		if (!attributes)
			return;

		SCR_ItemAttributeCollection ACMAttributes = SCR_ItemAttributeCollection.Cast(attributes);
		if (ACMAttributes)
			AppendNormalizedTypeNames(ACMAttributes.ACM_GetAdditionalCommonItemTypeNames(), resolvedTypes);

		if (resolvedTypes.IsEmpty())
			AppendNormalizedType(resolvedTypes, attributes.GetCommonType());
	}

	protected static void CollectNormalizedResourceCommonTypes(ResourceName resourceName, inout array<ECommonItemType> resolvedTypes)
	{
		if (resourceName.IsEmpty())
			return;

		ACM_EnsureResourceTypeCache();

		string cacheKey = resourceName;
		array<ECommonItemType> cached;
		if (s_mResourceCommonTypeCache.Find(cacheKey, cached) && cached)
		{
			foreach (ECommonItemType c : cached)
				AppendNormalizedType(resolvedTypes, c);
			return;
		}

		Resource entityResource = Resource.Load(resourceName);
		if (!entityResource.IsValid())
			return;

		BaseResourceObject resourceObject = entityResource.GetResource();
		if (!resourceObject)
			return;

		IEntitySource entitySource = resourceObject.ToEntitySource();
		if (!entitySource)
			return;

		// Compute into a local list so the fully-resolved result can be cached. Only a
		// fully-read prefab source reaches this point, so caching here never poisons
		// classification with a transient (JIP / early-init) load failure.
		array<ECommonItemType> computed = {};

		ECommonItemType overrideType;
		if (TryGetNormalizedOverrideType(entitySource, overrideType))
			AppendNormalizedType(computed, overrideType);

		for (int i = 0, componentCount = entitySource.GetComponentCount(); i < componentCount; i++)
		{
			IEntityComponentSource componentSource = entitySource.GetComponent(i);
			if (!componentSource)
				continue;

			typename sourceType = componentSource.GetClassName().ToType();
			if (!sourceType)
				continue;

			if (!sourceType.IsInherited(InventoryItemComponent) && !sourceType.IsInherited(BaseInventoryStorageComponent))
				continue;

			BaseContainer attributes = componentSource.GetObject("Attributes");
			if (!attributes)
				continue;

			array<string> additionalTypeNames = {};
			if (attributes.Get("m_ACM_AdditionalCommonItemTypes", additionalTypeNames))
			{
				array<ECommonItemType> additionalTypes = {};
				AppendNormalizedTypeNames(additionalTypeNames, additionalTypes);
				if (!additionalTypes.IsEmpty())
				{
					foreach (ECommonItemType additionalType : additionalTypes)
					{
						AppendNormalizedType(computed, additionalType);
					}

					continue;
				}
			}

			string commonTypeName;
			if (attributes.Get("CommonItemType", commonTypeName))
				AppendNormalizedTypeName(computed, commonTypeName);
		}

		s_mResourceCommonTypeCache.Set(cacheKey, computed);

		foreach (ECommonItemType t : computed)
			AppendNormalizedType(resolvedTypes, t);
	}

	protected static bool TryGetWeaponType(IEntity item, out EWeaponType resolvedWeaponType)
	{
		resolvedWeaponType = EWeaponType.WT_NONE;

		if (!item)
			return false;

		WeaponComponent weaponComponent = WeaponComponent.Cast(item.FindComponent(WeaponComponent));
		if (!weaponComponent)
			return false;

		resolvedWeaponType = weaponComponent.GetWeaponType();
		return true;
	}

	// Keeps ACM rifle logic compatible with mods that classify precision rifles under sniper/marksman weapon enums.
	protected static bool IsRifleFamilyWeaponType(EWeaponType weaponType)
	{
		if (weaponType == EWeaponType.WT_RIFLE)
			return true;

		string weaponTypeName = weaponType.ToString();
		if (weaponTypeName.Contains("SNIPER") || weaponTypeName.Contains("Sniper"))
			return true;

		if (weaponTypeName.Contains("MARKSMAN") || weaponTypeName.Contains("Marksman"))
			return true;

		return false;
	}
}
