//
//  ACM
//  ACM Holster Storage Slot
//
//  Original ACM-owned script for the modular framework.
//==================================================================================================

class ACM_HolsterStorageSlot : ACM_ConfiguredWeaponStorageSlot
{
	override int ACM_GetRoutePriority(IEntity item)
	{
		array<EWeaponType> allowedWeaponTypes = {};
		ACM_GetAllowedWeaponTypes(allowedWeaponTypes);

		int highestPriority = 0;
		foreach (EWeaponType allowedWeaponType : allowedWeaponTypes)
		{
			int priority = ACM_StoragePolicy.GetHolsterRoutePriority(item, allowedWeaponType);
			if (priority > highestPriority)
				highestPriority = priority;
		}

		return highestPriority;
	}

	override protected EWeaponType ACM_GetDefaultWeaponType()
	{
		return EWeaponType.WT_HANDGUN;
	}

	override protected bool ACM_MatchesStoredItem(IEntity item)
	{
		array<EWeaponType> allowedWeaponTypes = {};
		ACM_GetAllowedWeaponTypes(allowedWeaponTypes);

		foreach (EWeaponType allowedWeaponType : allowedWeaponTypes)
		{
			if (ACM_StoragePolicy.MatchesHolsterSidearm(item, allowedWeaponType))
				return true;
		}

		return false;
	}
}
