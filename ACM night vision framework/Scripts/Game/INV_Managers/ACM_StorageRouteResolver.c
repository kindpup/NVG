//
//  ACM
//  ACM Storage Route Resolver
//
//  Original ACM-owned script for the modular framework.
//==================================================================================================

// Shared route resolver for ACM-managed storage. Slot policy decides what is valid; this class only collects, scores, and executes routes.
class ACM_StorageRouteResolver
{
	// Scans all managed storages and returns the best currently empty slot for the item.
	static ACM_StorageRouteCandidate ResolveBestCandidate(SCR_InventoryStorageManagerComponent inventoryManager, IEntity item)
	{
		return ResolveBestCandidateInternal(inventoryManager, item, false);
	}

	// Scans all managed storages and allows occupied-slot replacement candidates when requested.
	static ACM_StorageRouteCandidate ResolveBestCandidateForMove(SCR_InventoryStorageManagerComponent inventoryManager, IEntity item, bool allowOccupiedReplacement = true)
	{
		return ResolveBestCandidateInternal(inventoryManager, item, allowOccupiedReplacement);
	}

	protected static ACM_StorageRouteCandidate ResolveBestCandidateInternal(
		SCR_InventoryStorageManagerComponent inventoryManager,
		IEntity item,
		bool allowOccupiedReplacement)
	{
		if (!inventoryManager || !item)
			return null;

		array<BaseInventoryStorageComponent> storages = {};
		inventoryManager.GetStorages(storages);

		array<ref ACM_StorageRouteCandidate> candidates = {};
		CollectCandidates(item, storages, candidates, allowOccupiedReplacement);

		return SelectBestCandidate(candidates);
	}

	// Executes the chosen route as either a move or a fresh insert, depending on how the request entered the resolver.
	static bool TryRouteToCandidate(
		SCR_InventoryStorageManagerComponent inventoryManager,
		IEntity item,
		ACM_StorageRouteCandidate candidate,
		bool moveExistingItem = false,
		SCR_InvCallBack cb = null)
	{
		if (!inventoryManager || !item || !candidate || !candidate.IsValid())
			return false;

		// Defense-in-depth family gate. The policy router already filters by family, but other
		// callers (interceptor / sidearm router) reuse TryRouteToCandidate. Block any route that
		// would land a resolvable-family item in an ACM slot of a different family. NONE-category
		// slots are treated as AMMO so RADIO / SMOKE / GRENADE can never fall into a generic
		// placard cell as a vanilla fallback.
		ACM_EquipmentStorageSlot ACMSlot = ACM_EquipmentStorageSlot.Cast(candidate.Slot);
		if (ACMSlot)
		{
			ACM_EItemFamily itemFamily = ACM_ItemClassificationService.ACM_ResolveItemFamily(item);
			if (itemFamily != ACM_EItemFamily.NONE)
			{
				ECommonItemType slotCategory = ACMSlot.ACM_GetSlotMagCategory();
				ACM_EItemFamily slotFamily = ACM_ItemClassificationService.ACM_SlotCategoryToFamily(slotCategory);
				if (slotFamily == ACM_EItemFamily.NONE)
					slotFamily = ACM_EItemFamily.AMMO;

				if (itemFamily != slotFamily)
				{
					Print(string.Format("[ACM ROUTER BLOCKED] Family mismatch itemFamily=%1 slotFamily=%2 cat=%3. Blocking route and vanilla fallback.",
						itemFamily, slotFamily, slotCategory), LogLevel.ERROR);
					return false;
				}
			}
		}

		if (moveExistingItem)
		{
			ACM_ReplicationDiag.StepStorage("3.REQUEST TryMoveItemToStorage", item, candidate.Storage, candidate.SlotIndex);
			bool moveResult = inventoryManager.TryMoveItemToStorage(item, candidate.Storage, candidate.SlotIndex,
				ACM_ReplicationDiag.WrapCallback(cb, "3.REQUEST TryMoveItemToStorage", item));
			if (ACM_MagGrenadePolicyRouter.ACM_DEBUG_ROUTING)
			{
				string itemName = "<no-prefab>";
				EntityPrefabData prefabData = item.GetPrefabData();
				if (prefabData)
					itemName = prefabData.GetPrefabName();
				Print(string.Format("[ACM Route POST] mode=move item=%1 slotIdx=%2 score=%3 result=%4",
					itemName, candidate.SlotIndex, candidate.Score, moveResult), LogLevel.NORMAL);
			}
			return moveResult;
		}

		ACM_ReplicationDiag.StepStorage("3.REQUEST TryInsertItemInStorage", item, candidate.Storage, candidate.SlotIndex);
		bool insertResult = inventoryManager.TryInsertItemInStorage(item, candidate.Storage, candidate.SlotIndex,
			ACM_ReplicationDiag.WrapCallback(cb, "3.REQUEST TryInsertItemInStorage", item));
		if (ACM_MagGrenadePolicyRouter.ACM_DEBUG_ROUTING)
		{
			string itemName = "<no-prefab>";
			EntityPrefabData prefabData = item.GetPrefabData();
			if (prefabData)
				itemName = prefabData.GetPrefabName();
			Print(string.Format("[ACM Route POST] mode=insert item=%1 slotIdx=%2 score=%3 result=%4",
				itemName, candidate.SlotIndex, candidate.Score, insertResult), LogLevel.NORMAL);
		}
		return insertResult;
	}

	// Gathers ACM-managed slots that currently accept the item, optionally including replaceable occupied slots.
	protected static void CollectCandidates(
		IEntity item,
		array<BaseInventoryStorageComponent> storages,
		array<ref ACM_StorageRouteCandidate> outCandidates,
		bool allowOccupiedReplacement = false)
	{
		foreach (BaseInventoryStorageComponent storage : storages)
		{
			if (!storage)
				continue;

			for (int slotIndex = 0; slotIndex < storage.GetSlotsCount(); slotIndex++)
			{
				InventoryStorageSlot slot = storage.GetSlot(slotIndex);
				if (!slot)
					continue;

				IEntity attachedEntity = slot.GetAttachedEntity();
				bool isReplacement = attachedEntity != null;
				if (isReplacement && attachedEntity == item)
					continue;

				if (isReplacement)
				{
					if (!allowOccupiedReplacement)
						continue;

					if (!storage.CanReplaceItem(item, slotIndex))
						continue;
				}
				else
				{
					if (!storage.CanStoreItem(item, slotIndex))
						continue;
				}

				outCandidates.Insert(new ACM_StorageRouteCandidate(
					storage,
					slot,
					slotIndex,
					ScoreCandidate(item, slot),
					isReplacement
				));
			}
		}
	}

	// Chooses the highest-scoring valid candidate, preferring empty slots over replacement candidates when scores tie.
	static ACM_StorageRouteCandidate SelectBestCandidate(array<ref ACM_StorageRouteCandidate> candidates)
	{
		ACM_StorageRouteCandidate bestCandidate = null;

		foreach (ACM_StorageRouteCandidate candidate : candidates)
		{
			if (!candidate || !candidate.IsValid())
				continue;

			if (!bestCandidate || candidate.Score > bestCandidate.Score)
			{
				bestCandidate = candidate;
				continue;
			}

			if (candidate.Score == bestCandidate.Score && bestCandidate.IsReplacement && !candidate.IsReplacement)
				bestCandidate = candidate;
		}

		return bestCandidate;
	}

	// Asks the slot rule for its routing priority so slot classes stay in control of destination preference.
	protected static int ScoreCandidate(IEntity item, InventoryStorageSlot slot)
	{
		SCR_EquipmentStorageSlot ruleSlot = SCR_EquipmentStorageSlot.Cast(slot);
		if (!ruleSlot)
			return 0;

		return ruleSlot.ACM_GetRoutePriority(item);
	}

}
