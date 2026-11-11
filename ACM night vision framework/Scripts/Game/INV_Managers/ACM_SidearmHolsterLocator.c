//
//  ACM
//  ACM Sidearm Holster Locator
//
//  Original ACM-owned script for the modular framework.
//==================================================================================================

class ACM_SidearmHolsterLocator
{
	static bool IsStoredInHolster(SCR_InventoryStorageManagerComponent inventoryManager, IEntity sidearm)
	{
		if (!inventoryManager || !sidearm)
			return false;

		array<BaseInventoryStorageComponent> storages = {};
		inventoryManager.GetStorages(storages);

		foreach (BaseInventoryStorageComponent storage : storages)
		{
			if (!storage)
				continue;

			for (int slotIndex = 0; slotIndex < storage.GetSlotsCount(); slotIndex++)
			{
				ACM_HolsterStorageSlot holsterSlot = ACM_HolsterStorageSlot.Cast(storage.GetSlot(slotIndex));
				if (!holsterSlot)
					continue;

				if (holsterSlot.GetAttachedEntity() == sidearm)
					return true;
			}
		}

		return false;
	}
}
