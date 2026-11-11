//
//  ACM
//  ACM Storage Slot Rule
//
//  Original ACM-owned script for the modular framework.
//==================================================================================================

modded class SCR_EquipmentStorageSlot
{
	bool ACM_CanAcceptItem(IEntity item)
	{
		return false;
	}

	bool ACM_CanAcceptResource(ResourceName resourceName)
	{
		return false;
	}

	// Explicit slot replay is used by vanilla arsenal/BLE loadout restore paths.
	// The default remains normal slot acceptance; ACM subclasses can override
	// to ignore logical occupancy groups while still preserving item policy.
	bool ACM_CanAcceptExplicitReplayItem(IEntity item)
	{
		return ACM_CanAcceptItem(item);
	}

	bool ACM_CanAcceptExplicitReplayResource(ResourceName resourceName)
	{
		return ACM_CanAcceptResource(resourceName);
	}

	bool ACM_CanReplaceWith(IEntity nextItem)
	{
		return false;
	}

	int ACM_GetRoutePriority(IEntity item)
	{
		return 0;
	}
}
