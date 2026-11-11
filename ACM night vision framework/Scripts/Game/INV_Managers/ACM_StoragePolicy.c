//
//  ACM
//  ACM Storage Policy
//
//  Original ACM-owned script for the modular framework.
//==================================================================================================

// Central policy layer for ACM inventory rules. Components and slots call into this class so routing logic stays reusable and engine hooks stay thin.
class ACM_StoragePolicy
{
	// Parses config type names and merges them into an enum list without duplicates.
	static void AppendParsedTypes(array<string> sourceTypes, inout array<ECommonItemType> targetTypes)
	{
		array<ECommonItemType> parsedTypes = {};
		ParseCommonItemTypes(sourceTypes, parsedTypes);

		foreach (ECommonItemType parsedType : parsedTypes)
		{
			if (targetTypes.Find(parsedType) == -1)
				targetTypes.Insert(parsedType);
		}
	}

	// Compares one entity against a normalized common item type.
	static bool MatchesItemType(IEntity item, ECommonItemType expectedType)
	{
		return ACM_ItemClassificationService.ItemMatchesCommonType(item, expectedType);
	}

	// Resource-name variant of the common item type check used before an entity exists.
	static bool MatchesResourceType(ResourceName resourceName, ECommonItemType expectedType)
	{
		return ACM_ItemClassificationService.ResourceMatchesCommonType(resourceName, expectedType);
	}

	// Delegates weapon-type matching to the shared classification service.
	static bool MatchesWeaponType(IEntity item, EWeaponType expectedWeaponType)
	{
		return ACM_ItemClassificationService.MatchesWeaponType(item, expectedWeaponType);
	}

	// Special-cases holsters so only real sidearms match handgun slots, not every WT_HANDGUN weapon.
	static bool MatchesHolsterSidearm(IEntity item, EWeaponType expectedWeaponType)
	{
		if (expectedWeaponType != EWeaponType.WT_HANDGUN)
		{
			if (expectedWeaponType == EWeaponType.WT_RIFLE)
				return ACM_ItemClassificationService.IsRifle(item);

			return MatchesWeaponType(item, expectedWeaponType);
		}

		return ACM_ItemClassificationService.IsHolsterSidearm(item);
	}

	// Maps explosive slot weapon types onto the ACM explosive item categories used by config data.
	static bool MatchesExplosiveResource(ResourceName resourceName, EWeaponType expectedWeaponType)
	{
		return MatchesResourceType(resourceName, ACM_GetExplosiveItemType(expectedWeaponType));
	}

	// Accepts radios by either live component identity or ACM radio item tagging.
	static bool MatchesRadioItem(IEntity item)
	{
		if (ACM_ItemClassificationService.IsRadio(item))
			return true;

		return MatchesItemType(item, ECommonItemType.ACM_RADIO);
	}

	// Resource-name variant of the radio acceptance check.
	static bool MatchesRadioResource(ResourceName resourceName)
	{
		if (ACM_ItemClassificationService.ResourceHasComponent(resourceName, SCR_RadioComponent))
			return true;

		return MatchesResourceType(resourceName, ECommonItemType.ACM_RADIO);
	}

	// Matches patch entities against the dedicated ACM patch item type.
	static bool MatchesPatchItem(IEntity item)
	{
		return MatchesItemType(item, ECommonItemType.ACM_PATCH);
	}

	// Resource-name variant of the patch acceptance check.
	static bool MatchesPatchResource(ResourceName resourceName)
	{
		return MatchesResourceType(resourceName, ECommonItemType.ACM_PATCH);
	}

	// Gives holsters a strong route score only when the candidate item is a valid holster sidearm.
	static int GetHolsterRoutePriority(IEntity item, EWeaponType expectedWeaponType)
	{
		if (!MatchesHolsterSidearm(item, expectedWeaponType))
			return 0;

		return 100;
	}

	// Converts serialized type names into deduplicated ECommonItemType values.
	static void ParseCommonItemTypes(array<string> sourceTypes, out array<ECommonItemType> parsedTypes)
	{
		parsedTypes.Clear();
		if (!sourceTypes)
			return;

		foreach (string rawTypeName : sourceTypes)
		{
			string normalizedTypeName = ACM_ItemClassificationService.NormalizeCommonItemTypeName(rawTypeName);
			// NOTE: enum .ToString() yields the numeric string ("0"), not the label,
			// so compare against the literal "NONE" (and "0" for unedited combo
			// defaults) or a configured "NONE" entry would poison the allowed list
			// and make the slot reject everything.
			if (normalizedTypeName.IsEmpty() || normalizedTypeName == "NONE" || normalizedTypeName == "0")
				continue;

			int parsedType = typename.StringToEnum(ECommonItemType, normalizedTypeName);
			if (parsedType < 0)
				continue;

			ECommonItemType itemType = parsedType;
			if (itemType == ECommonItemType.NONE)
				continue;

			if (parsedTypes.Find(itemType) == -1)
				parsedTypes.Insert(itemType);
		}
	}

	// Checks whether a type is present after normalizing aliases to the same ACM canonical value.
	static bool IsItemTypeInAllowedList(ECommonItemType itemType, array<ECommonItemType> allowedTypes)
	{
		if (!allowedTypes || !allowedTypes.Count())
			return true;

		ECommonItemType normalizedType = ACM_ItemClassificationService.NormalizeCommonItemType(itemType);
		foreach (ECommonItemType allowedType : allowedTypes)
		{
			if (ACM_ItemClassificationService.NormalizeCommonItemType(allowedType) == normalizedType)
				return true;
		}

		return false;
	}

	// Shared entity acceptance test used by policy-driven slot implementations.
	static bool DoesItemMatchAllowedTypes(IEntity item, array<ECommonItemType> allowedTypes)
	{
		if (!allowedTypes || !allowedTypes.Count())
			return true;

		foreach (ECommonItemType allowedType : allowedTypes)
		{
			if (ACM_ItemClassificationService.ItemMatchesCommonType(item, allowedType))
				return true;
		}

		return false;
	}

	// Shared resource-name acceptance test used during preview or prefab-based slot checks.
	static bool DoesResourceMatchAllowedTypes(ResourceName resourceName, array<ECommonItemType> allowedTypes)
	{
		if (!allowedTypes || !allowedTypes.Count())
			return true;

		foreach (ECommonItemType allowedType : allowedTypes)
		{
			if (ACM_ItemClassificationService.ResourceMatchesCommonType(resourceName, allowedType))
				return true;
		}

		return false;
	}

	// Internal helper that translates explosive weapon types into the ACM item-type buckets used by grenade slots.
	protected static ECommonItemType ACM_GetExplosiveItemType(EWeaponType expectedWeaponType)
	{
		if (expectedWeaponType == EWeaponType.WT_SMOKEGRENADE)
			return ECommonItemType.ACM_SMOKE_GRENADE;

		return ECommonItemType.ACM_GRENADE;
	}
}
