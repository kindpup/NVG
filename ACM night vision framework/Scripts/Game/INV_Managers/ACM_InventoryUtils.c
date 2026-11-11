//
//
//  ACM
//  ACM Inventory Utils
//
//  Original ACM-owned script for the modular framework.
//==================================================================================================

/*!
	ACM inventory compatibility facade.

	Active ACM routing now lives in dedicated services:
	- ACM_ItemClassificationService for item/resource identity
	- ACM_StoragePolicy for slot acceptance rules
	- ACM_StorageTopologyService for managed/external storage graph checks
	- ACM_WeaponFeedService for weapon-slot probing
	- ACM_StoredItemLifecycleService for stored-item physics handling

	This facade stays only for backwards-compatible ACM call sites and external addons.
*/
class ACM_ModularInventoryUtils
{
	// Classification facade
	static string NormalizeCommonItemTypeName(string typeName)
	{
		return ACM_ItemClassificationService.NormalizeCommonItemTypeName(typeName);
	}

	static ECommonItemType NormalizeCommonItemType(ECommonItemType itemType)
	{
		return ACM_ItemClassificationService.NormalizeCommonItemType(itemType);
	}

	static ECommonItemType GetNormalizedCommonType(IEntity item)
	{
		return ACM_ItemClassificationService.GetNormalizedCommonType(item);
	}

	static ECommonItemType GetNormalizedResourceCommonType(ResourceName resourceName)
	{
		return ACM_ItemClassificationService.GetNormalizedResourceCommonType(resourceName);
	}

	static bool ResourceHasComponent(ResourceName resourceName, typename componentType)
	{
		return ACM_ItemClassificationService.ResourceHasComponent(resourceName, componentType);
	}

	static bool IsMagazine(IEntity item)
	{
		return ACM_ItemClassificationService.IsMagazine(item);
	}

	static bool IsExplosiveCharge(IEntity item)
	{
		return ACM_ItemClassificationService.IsExplosiveCharge(item);
	}

	static bool IsHandgun(IEntity item)
	{
		return ACM_ItemClassificationService.IsHandgun(item);
	}

	static bool IsRadio(IEntity item)
	{
		return ACM_ItemClassificationService.IsRadio(item);
	}

	static bool IsPatch(IEntity item)
	{
		return ACM_ItemClassificationService.IsPatch(item);
	}

	static bool IsAccessoryItem(IEntity item)
	{
		return ACM_ItemClassificationService.IsAccessoryItem(item);
	}

	static bool IsChestAccessory(IEntity item)
	{
		return ACM_ItemClassificationService.IsChestAccessory(item);
	}

	static bool IsChestAccessoryResource(ResourceName resourceName)
	{
		return ACM_ItemClassificationService.IsChestAccessoryResource(resourceName);
	}

	// Policy facade
	static void ParseCommonItemTypes(array<string> sourceTypes, out array<ECommonItemType> parsedTypes)
	{
		ACM_StoragePolicy.ParseCommonItemTypes(sourceTypes, parsedTypes);
	}

	static bool IsItemTypeInAllowedList(ECommonItemType itemType, array<ECommonItemType> allowedTypes)
	{
		return ACM_StoragePolicy.IsItemTypeInAllowedList(itemType, allowedTypes);
	}

	static bool DoesItemMatchAllowedTypes(IEntity item, array<ECommonItemType> allowedTypes)
	{
		return ACM_StoragePolicy.DoesItemMatchAllowedTypes(item, allowedTypes);
	}

	static bool DoesResourceMatchAllowedTypes(ResourceName resourceName, array<ECommonItemType> allowedTypes)
	{
		return ACM_StoragePolicy.DoesResourceMatchAllowedTypes(resourceName, allowedTypes);
	}

	// Storage topology facade
	static bool IsValidArsenalStorage(BaseInventoryStorageComponent fallbackStorage)
	{
		return ACM_StorageTopologyService.HasReachableACMStorage(fallbackStorage);
	}

	static bool IsACMStorage(BaseInventoryStorageComponent storage)
	{
		return ACM_StorageTopologyService.IsManagedACMStorage(storage);
	}

	static bool IsOwnedStorage(array<BaseInventoryStorageComponent> storages, BaseInventoryStorageComponent storage)
	{
		return ACM_StorageTopologyService.BelongsToManagedSet(storages, storage);
	}

	static bool IsOwnedStorage(SCR_InventoryStorageManagerComponent manager, BaseInventoryStorageComponent storage)
	{
		return ACM_StorageTopologyService.BelongsToManagedSet(manager, storage);
	}

	static bool HasAnyACMStorage(array<BaseInventoryStorageComponent> storages)
	{
		return ACM_StorageTopologyService.HasManagedACMStorage(storages);
	}

	static bool HasAnyACMStorage(SCR_InventoryStorageManagerComponent manager)
	{
		return ACM_StorageTopologyService.HasManagedACMStorage(manager);
	}

	static bool HasCompatibleACMStorage(array<BaseInventoryStorageComponent> storages, IEntity item)
	{
		return ACM_StorageTopologyService.HasCompatibleManagedACMStorage(storages, item);
	}

	static bool HasCompatibleACMStorage(SCR_InventoryStorageManagerComponent manager, IEntity item)
	{
		return ACM_StorageTopologyService.HasCompatibleManagedACMStorage(manager, item);
	}

	static bool HasACMSiblings(array<BaseInventoryStorageComponent> storages, BaseInventoryStorageComponent current)
	{
		return ACM_StorageTopologyService.HasParallelManagedACMStorage(storages, current);
	}

	static bool HasACMSiblings(SCR_InventoryStorageManagerComponent manager, BaseInventoryStorageComponent current)
	{
		return ACM_StorageTopologyService.HasParallelManagedACMStorage(manager, current);
	}

	static bool IsExternalStorage(BaseInventoryStorageComponent storage)
	{
		return ACM_StorageTopologyService.IsExternalTransferStorage(storage);
	}

	// Weapon feed facade
	static int GetWeaponMagazineSlotIndex(BaseInventoryStorageComponent storage, IEntity magazine)
	{
		return ACM_WeaponFeedService.FindFeedSlotIndex(storage, magazine);
	}

	static bool TryRouteMagazineIntoWeapon(
		SCR_InventoryStorageManagerComponent manager,
		BaseInventoryStorageComponent storage,
		IEntity magazine,
		SCR_InvCallBack callback = null)
	{
		return ACM_WeaponFeedService.TryFeedItemIntoWeapon(manager, storage, magazine, callback);
	}

	// Stored item lifecycle facade
	static void DisableAttachedEntityPhysics(IEntity entity, out int interactionLayer)
	{
		ACM_StoredItemLifecycleService.SuspendStoredItemPhysics(entity, interactionLayer);
	}

	static void RestoreAttachedEntityPhysics(IEntity entity, int interactionLayer)
	{
		ACM_StoredItemLifecycleService.RestoreStoredItemPhysics(entity, interactionLayer);
	}
}

/*!
	Replication authority guard for ACM inventory mutations.

	Slot callbacks (OnAttachedEntity / OnDetachedEntity) fire on EVERY machine:
	the server, the owning client, and every remote proxy client. Any code that
	reacts to those callbacks by mutating inventory (refill, rebound, auto-stow)
	must only run on the machine that owns the authoritative simulation, or the
	per-machine slot graphs diverge — items end up visible on one machine and
	missing on the others.
*/
class ACM_ReplicationAuthority
{
	//! True when this machine is the authority for the entity's replication
	//! hierarchy (server, or local/offline play). False on all proxy clients.
	static bool HasAuthorityOver(IEntity entity)
	{
		if (!entity)
			return false;

		IEntity root = entity.GetRootParent();
		if (!root)
			root = entity;

		RplComponent rpl = RplComponent.Cast(root.FindComponent(RplComponent));
		if (!rpl)
			return true; // No replication node: purely local entity.

		return !rpl.IsProxy();
	}

	//! True unless this machine is a remote proxy (neither authority nor owner).
	//! Use for flows that must keep running on the owning client (UI-driven,
	//! animation-driven) but must never run on machines merely observing.
	static bool IsLocalDriverFor(IEntity entity)
	{
		if (!entity)
			return false;

		IEntity root = entity.GetRootParent();
		if (!root)
			root = entity;

		RplComponent rpl = RplComponent.Cast(root.FindComponent(RplComponent));
		if (!rpl)
			return true;

		return !rpl.IsRemoteProxy();
	}
}
