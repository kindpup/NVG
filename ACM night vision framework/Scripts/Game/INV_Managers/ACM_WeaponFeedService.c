//
//  ACM
//  ACM Weapon Feed Service
//
//  Original ACM-owned script for the modular framework.
//==================================================================================================

/*!
	ACM weapon feed helpers.

	ACM treats weapon attachment storages as feed targets discovered through slot
	acceptance rather than hardcoded slot IDs.
*/
class ACM_WeaponFeedService
{
	static int FindFeedSlotIndex(BaseInventoryStorageComponent storage, IEntity item)
	{
		if (!storage || !item)
			return -1;

		for (int slotIndex = 0; slotIndex < storage.GetSlotsCount(); ++slotIndex)
		{
			if (storage.CanStoreItem(item, slotIndex))
				return slotIndex;
		}

		return -1;
	}

	static bool TryFeedItemIntoWeapon(
		SCR_InventoryStorageManagerComponent inventoryManager,
		BaseInventoryStorageComponent storage,
		IEntity item,
		SCR_InvCallBack callback = null)
	{
		if (!inventoryManager)
			return false;

		int slotIndex = FindFeedSlotIndex(storage, item);
		if (slotIndex < 0)
			return false;

		return inventoryManager.TryMoveItemToStorage(item, storage, slotIndex, callback);
	}
}
