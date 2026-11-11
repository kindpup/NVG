//
//  ACM
//  ACM Storage Topology Service
//
//  Original ACM-owned script for the modular framework.
//==================================================================================================

/*!
	ACM storage topology helpers.

	These helpers describe how ACM distinguishes managed player-carried storages from
	external storages and how it detects whether the current equipment graph can host
	ACM-specific slot routing.
*/
class ACM_StorageTopologyService
{
	static bool HasReachableACMStorage(BaseInventoryStorageComponent fallbackStorage)
	{
		if (!fallbackStorage)
			return false;

		IEntity owner = fallbackStorage.GetOwner();
		if (!owner)
			return false;

		IEntity root = owner.GetRootParent();
		if (!root)
			return false;

		SCR_InventoryStorageManagerComponent inventoryManager = SCR_InventoryStorageManagerComponent.Cast(root.FindComponent(SCR_InventoryStorageManagerComponent));
		if (!inventoryManager)
			return false;

		return HasManagedACMStorage(inventoryManager);
	}

	static bool IsManagedACMStorage(BaseInventoryStorageComponent storage)
	{
		return ACM_EquipmentStorageComponent.Cast(storage) != null;
	}

	static bool BelongsToManagedSet(array<BaseInventoryStorageComponent> storages, BaseInventoryStorageComponent storage)
	{
		if (!storage)
			return false;

		return storages.Contains(storage);
	}

	static bool BelongsToManagedSet(SCR_InventoryStorageManagerComponent inventoryManager, BaseInventoryStorageComponent storage)
	{
		if (!inventoryManager || !storage)
			return false;

		array<BaseInventoryStorageComponent> storages = {};
		inventoryManager.GetStorages(storages);
		return BelongsToManagedSet(storages, storage);
	}

	static bool HasManagedACMStorage(array<BaseInventoryStorageComponent> storages)
	{
		foreach (BaseInventoryStorageComponent storage : storages)
		{
			if (IsManagedACMStorage(storage))
				return true;
		}

		return false;
	}

	static bool HasManagedACMStorage(SCR_InventoryStorageManagerComponent inventoryManager)
	{
		if (!inventoryManager)
			return false;

		array<BaseInventoryStorageComponent> storages = {};
		inventoryManager.GetStorages(storages);
		return HasManagedACMStorage(storages);
	}

	static bool HasCompatibleManagedACMStorage(array<BaseInventoryStorageComponent> storages, IEntity item)
	{
		if (!item)
			return false;

		foreach (BaseInventoryStorageComponent storage : storages)
		{
			if (!IsManagedACMStorage(storage))
				continue;

			if (storage.CanStoreItem(item, -1))
				return true;
		}

		return false;
	}

	static bool HasCompatibleManagedACMStorage(SCR_InventoryStorageManagerComponent inventoryManager, IEntity item)
	{
		if (!inventoryManager || !item)
			return false;

		array<BaseInventoryStorageComponent> storages = {};
		inventoryManager.GetStorages(storages);
		return HasCompatibleManagedACMStorage(storages, item);
	}

	static bool HasParallelManagedACMStorage(array<BaseInventoryStorageComponent> storages, BaseInventoryStorageComponent current)
	{
		if (!current)
			return false;

		foreach (BaseInventoryStorageComponent storage : storages)
		{
			if (storage != current && IsManagedACMStorage(storage))
				return true;
		}

		return false;
	}

	static bool HasParallelManagedACMStorage(SCR_InventoryStorageManagerComponent inventoryManager, BaseInventoryStorageComponent current)
	{
		if (!inventoryManager || !current)
			return false;

		array<BaseInventoryStorageComponent> storages = {};
		inventoryManager.GetStorages(storages);
		return HasParallelManagedACMStorage(storages, current);
	}

	static bool IsExternalTransferStorage(BaseInventoryStorageComponent storage)
	{
		if (!storage)
			return false;

		return SCR_UniversalInventoryStorageComponent.Cast(storage) != null;
	}
}
