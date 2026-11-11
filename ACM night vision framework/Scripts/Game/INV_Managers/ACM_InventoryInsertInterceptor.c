//
//  ACM
//  ACM Inventory Insert Interceptor
//
//  Original ACM-owned script for the modular framework.
//==================================================================================================

enum ACM_EInsertDecision
{
	PASS_THROUGH,
	ROUTE_WEAPON_ATTACHMENT,
	ROUTE_SIDEARM
}

// Captures one engine insert/move request so ACM can evaluate routing without reading mutable manager state repeatedly.
class ACM_InventoryInsertRequest
{
	IEntity Item;
	BaseInventoryStorageComponent StorageTo;
	BaseInventoryStorageComponent StorageFrom;
	SCR_InvCallBack Callback;
	ref array<BaseInventoryStorageComponent> ManagedStorages;

	// Stores the raw engine request values and prepares the managed-storage cache used by topology checks.
	void ACM_InventoryInsertRequest(
		IEntity item,
		BaseInventoryStorageComponent storageTo = null,
		BaseInventoryStorageComponent storageFrom = null,
		SCR_InvCallBack cb = null)
	{
		Item = item;
		StorageTo = storageTo;
		StorageFrom = storageFrom;
		Callback = cb;
		ManagedStorages = {};
	}

	// Snapshots the caller's storages so later decisions work against one consistent managed-storage set.
	void CaptureManagedStorages(SCR_InventoryStorageManagerComponent inventoryManager)
	{
		ManagedStorages.Clear();

		if (!inventoryManager)
			return;

		inventoryManager.GetStorages(ManagedStorages);
	}

	// Distinguishes a move between storages from a fresh insert originating outside the inventory manager.
	bool IsMoveRequest()
	{
		return StorageFrom != null;
	}

	// Treats null destinations as manager-controlled inserts and otherwise checks whether the target belongs to ACM-managed storage.
	bool TargetsManagedStorage()
	{
		return (!StorageTo) || ACM_StorageTopologyService.BelongsToManagedSet(ManagedStorages, StorageTo);
	}
}

// Decision layer for the inventory hook. It decides whether ACM should reroute the request or let Reforger handle it normally.
class ACM_InventoryInsertInterceptor
{
	// Examines one request and returns the smallest ACM-specific action needed for it.
	static ACM_EInsertDecision BuildInsertDecision(
		SCR_InventoryStorageManagerComponent inventoryManager,
		ACM_InventoryInsertRequest request)
	{
		if (!inventoryManager || !request || !request.Item)
			return ACM_EInsertDecision.PASS_THROUGH;

		if (SCR_WeaponAttachmentsStorageComponent.Cast(request.StorageTo))
			return ACM_EInsertDecision.ROUTE_WEAPON_ATTACHMENT;

		request.CaptureManagedStorages(inventoryManager);

		if (!ACM_StorageTopologyService.HasManagedACMStorage(request.ManagedStorages))
			return ACM_EInsertDecision.PASS_THROUGH;

		if (IsExternalTransfer(request))
			return ACM_EInsertDecision.PASS_THROUGH;

		if (IsOwnedSidearmDeposit(request))
			return ACM_EInsertDecision.ROUTE_SIDEARM;

		return ACM_EInsertDecision.PASS_THROUGH;
	}

	// Dispatches the selected decision to the matching handler.
	static bool TryHandleDecision(
		SCR_InventoryStorageManagerComponent inventoryManager,
		ACM_EInsertDecision decision,
		ACM_InventoryInsertRequest request)
	{
		switch (decision)
		{
			case ACM_EInsertDecision.ROUTE_WEAPON_ATTACHMENT:
				return TryHandleWeaponAttachmentInsert(inventoryManager, request);

			case ACM_EInsertDecision.ROUTE_SIDEARM:
				return TryHandleSidearmInsert(inventoryManager, request);
		}

		return false;
	}

	// Preserves weapon-feed behavior by finding the real compatible attachment slot before issuing the move.
	protected static bool TryHandleWeaponAttachmentInsert(
		SCR_InventoryStorageManagerComponent inventoryManager,
		ACM_InventoryInsertRequest request)
	{
		if (!inventoryManager || !request || !request.Item)
			return false;

		SCR_WeaponAttachmentsStorageComponent weaponStorage = SCR_WeaponAttachmentsStorageComponent.Cast(request.StorageTo);
		if (!weaponStorage)
			return false;

		int slotIndex = ACM_WeaponFeedService.FindFeedSlotIndex(weaponStorage, request.Item);
		if (slotIndex < 0)
			return false;

		if (request.IsMoveRequest())
			return inventoryManager.TryMoveItemToStorage(request.Item, weaponStorage, slotIndex, request.Callback);

		return inventoryManager.TryInsertItemInStorage(request.Item, weaponStorage, slotIndex, request.Callback);
	}

	// Routes a managed sidearm through the shared slot resolver instead of trusting raw insertion order.
	protected static bool TryHandleSidearmInsert(
		SCR_InventoryStorageManagerComponent inventoryManager,
		ACM_InventoryInsertRequest request)
	{
		if (!inventoryManager || !request || !request.Item)
			return false;

		ACM_StorageRouteCandidate routeCandidate = ACM_StorageRouteResolver.ResolveBestCandidate(inventoryManager, request.Item);
		return ACM_StorageRouteResolver.TryRouteToCandidate(
			inventoryManager,
			request.Item,
			routeCandidate,
			request.IsMoveRequest(),
			request.Callback
		);
	}

	// Lets transfers from ACM-managed storage into external containers bypass ACM auto-routing.
	protected static bool IsExternalTransfer(ACM_InventoryInsertRequest request)
	{
		if (!request || !request.StorageTo)
			return false;

		if (!ACM_StorageTopologyService.IsExternalTransferStorage(request.StorageTo))
			return false;

		if (!request.StorageFrom)
			return false;

		return ACM_StorageTopologyService.BelongsToManagedSet(request.ManagedStorages, request.StorageFrom);
	}

	// Marks handgun deposits targeting managed storage as sidearm-routing candidates.
	protected static bool IsOwnedSidearmDeposit(ACM_InventoryInsertRequest request)
	{
		if (!request || !request.Item)
			return false;

		if (!request.TargetsManagedStorage())
			return false;

		BaseWeaponComponent pistolComponent = BaseWeaponComponent.Cast(request.Item.FindComponent(BaseWeaponComponent));
		if (!pistolComponent)
			return false;

		return pistolComponent.GetWeaponType() == EWeaponType.WT_HANDGUN;
	}
}
