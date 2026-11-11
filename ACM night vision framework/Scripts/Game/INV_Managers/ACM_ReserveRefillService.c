//
//  ACM
//  ACM Reserve Refill Service
//
//  Original ACM-owned script for the modular framework.
//==================================================================================================

class ACM_ReserveRefillService
{
	static bool TryRefillSlot(ACM_EquipmentStorageSlot visibleSlot)
	{
		if (!visibleSlot || visibleSlot.GetAttachedEntity())
			return false;

		ACM_EquipmentStorageComponent sourceStorage = ACM_EquipmentStorageComponent.Cast(visibleSlot.GetStorage());
		if (!sourceStorage)
			return false;

		IEntity slotOwner = sourceStorage.GetOwner();
		if (!slotOwner)
			return false;

		IEntity playerRoot = slotOwner.GetRootParent();
		if (!playerRoot)
			return false;

		// REPLICATION: refill must only ever be initiated by the authority. Slot
		// callbacks fire on every machine, and a client-side refill would mutate a
		// slot graph the server does not know about (items visible only locally).
		if (!ACM_ReplicationAuthority.HasAuthorityOver(playerRoot))
			return false;

		SCR_InventoryStorageManagerComponent inventoryManager = SCR_InventoryStorageManagerComponent.Cast(playerRoot.FindComponent(SCR_InventoryStorageManagerComponent));
		if (!inventoryManager)
			return false;

		array<BaseInventoryStorageComponent> reserveStorages = {};
		BuildReserveSearchScope(inventoryManager, playerRoot, sourceStorage, reserveStorages);

		IEntity reserveItem = FindBestReserveForSlot(visibleSlot, reserveStorages);
		if (!reserveItem)
			return false;

		return TryConsumeReserveIntoSlot(inventoryManager, reserveItem, visibleSlot, sourceStorage);
	}

	protected static void BuildReserveSearchScope(
		SCR_InventoryStorageManagerComponent inventoryManager,
		IEntity playerRoot,
		ACM_EquipmentStorageComponent sourceStorage,
		array<BaseInventoryStorageComponent> outStorages)
	{
		array<BaseInventoryStorageComponent> allStorages = {};
		inventoryManager.GetStorages(allStorages);

		foreach (BaseInventoryStorageComponent storage : allStorages)
		{
			if (!IsEligibleReserveStorage(storage, playerRoot, sourceStorage))
				continue;

			outStorages.Insert(storage);
		}
	}

	protected static IEntity FindBestReserveForSlot(
		ACM_EquipmentStorageSlot visibleSlot,
		array<BaseInventoryStorageComponent> storages)
	{
		foreach (BaseInventoryStorageComponent storage : storages)
		{
			for (int slotIndex = 0; slotIndex < storage.GetSlotsCount(); slotIndex++)
			{
				InventoryStorageSlot reserveSlot = storage.GetSlot(slotIndex);
				if (!reserveSlot)
					continue;

				IEntity item = reserveSlot.GetAttachedEntity();
				if (!item)
					continue;

				if (!item.FindComponent(InventoryMagazineComponent))
					continue;

				if (visibleSlot.ACM_CanAcceptItem(item))
					return item;
			}
		}

		return null;
	}

	protected static bool TryConsumeReserveIntoSlot(
		SCR_InventoryStorageManagerComponent inventoryManager,
		IEntity reserveItem,
		ACM_EquipmentStorageSlot visibleSlot,
		ACM_EquipmentStorageComponent targetStorage)
	{
		if (!inventoryManager || !reserveItem || !visibleSlot || !targetStorage)
			return false;

		// REPLICATION: the move must go through the inventory manager so the engine
		// generates a replicated inventory operation. The previous implementation
		// used raw reserveSlot.DetachEntity() + visibleSlot.AttachEntity(), which
		// mutates only the local machine's slot graph and never replicates — the
		// direct cause of "I see the item, other players don't" desyncs.
		ACM_ReplicationDiag.StepStorage("6.REFILL TryMoveItemToStorage reserve -> visible slot", reserveItem, targetStorage, visibleSlot.GetID());
		return inventoryManager.TryMoveItemToStorage(reserveItem, targetStorage, visibleSlot.GetID(),
			ACM_ReplicationDiag.WrapCallback(null, "6.REFILL TryMoveItemToStorage", reserveItem));
	}

	protected static bool IsEligibleReserveStorage(
		BaseInventoryStorageComponent storage,
		IEntity playerRoot,
		ACM_EquipmentStorageComponent sourceStorage)
	{
		if (!storage || storage == sourceStorage)
			return false;

		IEntity storageOwner = storage.GetOwner();
		if (!storageOwner)
			return false;

		if (storageOwner.GetRootParent() != playerRoot)
			return false;

		if (SCR_CharacterInventoryStorageComponent.Cast(storage))
			return false;

		if (ACM_EquipmentStorageComponent.Cast(storage))
			return false;

		if (!SCR_EquipmentStorageComponent.Cast(storage)
			&& !SCR_UniversalInventoryStorageComponent.Cast(storage)
			&& !ClothNodeStorageComponent.Cast(storage))
			return false;

		return true;
	}
}
