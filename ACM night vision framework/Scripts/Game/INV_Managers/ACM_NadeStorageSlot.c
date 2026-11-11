//
//  ACM
//  ACM Nade Storage Slot
//
//  Original ACM-owned script for the modular framework.
//==================================================================================================

class ACM_NadeStorageSlot : ACM_ConfiguredWeaponStorageSlot
{
	override protected EWeaponType ACM_GetDefaultWeaponType()
	{
		return EWeaponType.WT_FRAGGRENADE;
	}

	// Iterate ALL configured weapon types. The previous implementation only checked
	// ACM_GetAllowedWeaponType() (element [0]), silently ignoring every additional
	// entry — e.g. a slot configured [WT_FRAGGRENADE, WT_SMOKEGRENADE] never
	// accepted smoke grenades.
	override protected bool ACM_MatchesStoredItem(IEntity item)
	{
		array<EWeaponType> allowedWeaponTypes = {};
		ACM_GetAllowedWeaponTypes(allowedWeaponTypes);

		foreach (EWeaponType allowedWeaponType : allowedWeaponTypes)
		{
			if (ACM_StoragePolicy.MatchesWeaponType(item, allowedWeaponType))
				return true;
		}

		return false;
	}

	override protected bool ACM_MatchesStoredResource(ResourceName resourceName)
	{
		array<EWeaponType> allowedWeaponTypes = {};
		ACM_GetAllowedWeaponTypes(allowedWeaponTypes);

		foreach (EWeaponType allowedWeaponType : allowedWeaponTypes)
		{
			if (ACM_StoragePolicy.MatchesExplosiveResource(resourceName, allowedWeaponType))
				return true;
		}

		return false;
	}
}
