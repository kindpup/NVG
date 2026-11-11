//==================================================================================================
//  ACM
//  ACM Magazine & Grenade Policy Routing
//
//  Slot-driven routing: use the actual ACM_EquipmentStorageSlot entries for placement.
//  This keeps Prefab / Offset / Angles editable on the native storage slots where Workbench
//  gives you the normal visual placement tooling.
//==================================================================================================

class ACM_MagGrenadePolicyRouter
{
	// Toggle to print routing decisions to the script log. Leave false in shipping builds.
	static const bool ACM_DEBUG_ROUTING = false;

	// Returns the best candidate for a magazine or grenade, or null if none found.
	static ACM_StorageRouteCandidate ResolveBestPolicyCandidate(SCR_InventoryStorageManagerComponent inventoryManager, IEntity item)
	{
		if (!inventoryManager || !item)
			return null;

		array<BaseInventoryStorageComponent> storages = {};
		inventoryManager.GetStorages(storages);

		array<ref ACM_StorageRouteCandidate> candidates = {};
		CollectPolicyCandidates(item, storages, candidates);

		return ACM_StorageRouteResolver.SelectBestCandidate(candidates);
	}


	// Same as ResolveBestPolicyCandidate, but restricted to the storage the UI explicitly targeted.
	// This matters for BLE / arsenal flows where the user clicks a specific pouch storage directly.
	// Accepts any storage type — CollectPolicyCandidates gates per-slot so radio / grenade / patch
	// pouches (which live in ACM_PouchStorageComponent, not ACM_EquipmentStorageComponent) still work.
	static ACM_StorageRouteCandidate ResolveBestPolicyCandidateForStorage(BaseInventoryStorageComponent storage, IEntity item)
	{
		if (!storage || !item)
			return null;

		array<BaseInventoryStorageComponent> storages = {};
		storages.Insert(storage);

		array<ref ACM_StorageRouteCandidate> candidates = {};
		CollectPolicyCandidates(item, storages, candidates);

		return ACM_StorageRouteResolver.SelectBestCandidate(candidates);
	}

	// Collects policy-driven storage slots. Two slot families participate:
	//   1. ACM_EquipmentStorageSlot (mag/utility cells on placards) — only valid when the
	//      hosting storage is an ACM_EquipmentStorageComponent flagged policy-driven.
	//   2. ACM_PolicyDrivenStorageSlot (ACM_RadioStorageSlot / ACM_NadeStorageSlot /
	//      ACM_PatchStorageSlot / ACM_HolsterStorageSlot) — these slots live in arbitrary
	//      storage components (commonly ACM_PouchStorageComponent for radio / grenade
	//      pouches) and ARE the policy themselves, so we accept them regardless of the
	//      hosting storage type.
	protected static void CollectPolicyCandidates(
		IEntity item,
		array<BaseInventoryStorageComponent> storages,
		array<ref ACM_StorageRouteCandidate> outCandidates)
	{
		foreach (BaseInventoryStorageComponent storage : storages)
		{
			if (!storage)
				continue;

			ACM_EquipmentStorageComponent ACMStorage = ACM_EquipmentStorageComponent.Cast(storage);
			bool isPolicyDrivenEquipmentStorage = ACMStorage && ACMStorage.ACM_IsPolicyDriven();
			bool hasEmbeddedEquipmentPolicySlots = !ACMStorage && ACM_HasEquipmentPolicySlots(storage);

			for (int slotIndex = 0; slotIndex < storage.GetSlotsCount(); slotIndex++)
			{
				InventoryStorageSlot slot = storage.GetSlot(slotIndex);
				if (!slot)
					continue;

				SCR_EquipmentStorageSlot ruleSlot = SCR_EquipmentStorageSlot.Cast(slot);
				if (!ruleSlot)
					continue;

				ACM_EquipmentStorageSlot ACMSlot = ACM_EquipmentStorageSlot.Cast(slot);
				ACM_PolicyDrivenStorageSlot policySlot = ACM_PolicyDrivenStorageSlot.Cast(slot);

				// Mag/utility cells are only valid inside policy-driven equipment storages.
				// Radio / grenade / patch / holster slots are valid anywhere they exist.
				if (ACMSlot && !isPolicyDrivenEquipmentStorage && !hasEmbeddedEquipmentPolicySlots)
					continue;
				if (!ACMSlot && !policySlot)
					continue;

				if (ACMSlot)
				{
					if (ACMSlot.ACM_IsOccupiedOrGroupBlocked())
						continue;

					if (!ACMSlot.ACM_MatchesPolicyItem(item))
						continue;
				}
				else
				{
					if (slot.GetAttachedEntity())
						continue;

					if (!ruleSlot.ACM_CanAcceptItem(item))
						continue;
				}

				if (!storage.CanStoreItem(item, slotIndex))
					continue;

				int score = ScorePolicyCandidate(item, ruleSlot, ACMSlot, storage);
				outCandidates.Insert(new ACM_StorageRouteCandidate(
					storage,
					slot,
					slotIndex,
					score,
					false
				));

				if (ACM_DEBUG_ROUTING)
					DebugLogCandidate(item, storage, slot, slotIndex, ACMSlot, score);
			}
		}
	}

	protected static bool ACM_HasEquipmentPolicySlots(BaseInventoryStorageComponent storage)
	{
		if (!storage)
			return false;

		for (int slotIndex = 0; slotIndex < storage.GetSlotsCount(); slotIndex++)
		{
			if (ACM_EquipmentStorageSlot.Cast(storage.GetSlot(slotIndex)))
				return true;
		}

		return false;
	}

	// Prevents duplicate logical slots from being filled.
	// Example: STANAG_01, DMR_01, and MG_01 can share Occupancy Group "Placard_Cell_01".
	// Once any one of those slots has an attached entity, the other variants are blocked.
	protected static bool IsOccupancyBlocked(BaseInventoryStorageComponent storage, ACM_EquipmentStorageSlot candidateSlot)
	{
		if (!storage || !candidateSlot)
			return true;

		if (candidateSlot.GetAttachedEntity())
			return true;

		string group = candidateSlot.ACM_GetOccupancyGroup();
		if (group.IsEmpty() || group == "NONE")
			return false;

		for (int slotIndex = 0; slotIndex < storage.GetSlotsCount(); slotIndex++)
		{
			InventoryStorageSlot slot = storage.GetSlot(slotIndex);
			if (!slot)
				continue;

			ACM_EquipmentStorageSlot ACMSlot = ACM_EquipmentStorageSlot.Cast(slot);
			if (!ACMSlot)
				continue;

			if (ACMSlot.ACM_GetOccupancyGroup() != group)
				continue;

			if (slot.GetAttachedEntity())
				return true;
		}

		return false;
	}

	protected static int ScorePolicyCandidate(IEntity item, SCR_EquipmentStorageSlot slot, ACM_EquipmentStorageSlot ACMSlot = null, BaseInventoryStorageComponent storage = null)
	{
		int score = 100;

		// Exact prefab slots win over generic category slots.
		if (ACMSlot && !ACMSlot.ACM_GetItemPrefabRule().IsEmpty())
			score += 1000;

		if (slot)
			score += slot.ACM_GetRoutePriority(item);

		// Family-aware scoring. Compatibility filtering already happens in IsItemCompatible /
		// ACM_MatchesPolicyItem, so by the time we score the candidate we know the families
		// match. We still grade exact-category against same-family-fallback so a slot
		// configured for ACM_RIFLE_AMMO always beats a generic AMMO slot for a rifle mag.
		ACM_EItemFamily itemFamily = ACM_ItemClassificationService.ACM_ResolveItemFamily(item);
		if (ACMSlot)
		{
			ECommonItemType slotCategory = ACMSlot.ACM_GetSlotMagCategory();
			ACM_EItemFamily slotFamily = ACM_ItemClassificationService.ACM_SlotCategoryToFamily(slotCategory);

			if (itemFamily == ACM_EItemFamily.AMMO
				&& slotFamily == ACM_EItemFamily.AMMO
				&& slotCategory != ECommonItemType.NONE
				&& ACM_ItemClassificationService.ItemMatchesCommonType(item, slotCategory))
			{
				score += 700; // exact ammo category
			}
			else if (slotFamily != ACM_EItemFamily.NONE && slotFamily == itemFamily)
			{
				score += 500; // exact utility family (smoke/grenade/radio) or generic same-family ammo
			}
			else if (slotFamily == ACM_EItemFamily.NONE && itemFamily == ACM_EItemFamily.AMMO)
			{
				score += 200; // legacy NONE slot accepting any ammo as fallback
			}
		}

		// Dedicated single-slot grenade / radio pouches outrank a placard's built-in
		// fallback grenade/radio cells. We intentionally exclude single-slot mag pouches
		// (e.g. SLED 1x1) from this bonus so dragging a magazine onto a placard still
		// lands in the placard, not in a stray single-mag pouch elsewhere.
		bool isDedicatedGrenadeOrRadioSlot = ACM_NadeStorageSlot.Cast(slot) != null
			|| ACM_RadioStorageSlot.Cast(slot) != null;
		if (isDedicatedGrenadeOrRadioSlot && storage && storage.GetSlotsCount() == 1)
			score += 300;

		return score;
	}

	// Diagnostic helper. Only invoked when ACM_DEBUG_ROUTING is true.
	protected static void DebugLogCandidate(
		IEntity item,
		BaseInventoryStorageComponent storage,
		InventoryStorageSlot slot,
		int slotIndex,
		ACM_EquipmentStorageSlot ACMSlot,
		int score)
	{
		string itemPrefab = "<no-prefab>";
		EntityPrefabData prefabData = item.GetPrefabData();
		if (prefabData)
			itemPrefab = prefabData.GetPrefabName();

		string ownerName = "<no-owner>";
		IEntity ownerEntity = storage.GetOwner();
		if (ownerEntity)
		{
			EntityPrefabData ownerPrefab = ownerEntity.GetPrefabData();
			if (ownerPrefab)
				ownerName = ownerPrefab.GetPrefabName();
		}

		string slotID = "<no-id>";
		string occGroup = "<no-group>";
		string slotCategory = "<no-cat>";
		if (ACMSlot)
		{
			slotID = ACMSlot.ACM_GetSlotID();
			occGroup = ACMSlot.ACM_GetOccupancyGroup();
			slotCategory = ACMSlot.ACM_GetSlotMagCategory().ToString();
		}

		ACM_EItemFamily itemFamily = ACM_ItemClassificationService.ACM_ResolveItemFamily(item);
		string slotClassName = slot.Type().ToString();

		Print(string.Format("[ACM Router] item=%1 family=%2 owner=%3 slotIdx=%4 slotClass=%5 cat=%6 id=%7 grp=%8 score=%9",
			itemPrefab, itemFamily, ownerName, slotIndex, slotClassName, slotCategory, slotID, occGroup, score), LogLevel.NORMAL);
	}
}

