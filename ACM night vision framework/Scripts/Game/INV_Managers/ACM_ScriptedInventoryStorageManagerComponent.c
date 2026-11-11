//
//  ACM
//  ACM Scripted Inventory Storage Manager Component
//
//  Original ACM-owned script for the modular framework.
//==================================================================================================

// Reforger exposes item insertion through SCR_InventoryStorageManagerComponent.
// ACM keeps this hook thin and pushes compatibility and destination decisions into policy and routing services.
modded class SCR_InventoryStorageManagerComponent
{
	protected bool m_bACM_SuppressFeedback = false;

	// Lets internal reroutes reuse this hook without replaying pickup audio and gestures.
	void ACM_SuppressFeedback(bool suppress)
	{
		m_bACM_SuppressFeedback = suppress;
	}

	// Public compatibility wrapper so ACM slot code can avoid launching a second
	// reroute while the inventory manager is already processing another operation.
	bool ACM_IsInventoryCurrentlyLocked()
	{
		return IsInventoryLocked();
	}

	// Defensively isolates weapon-attachment validation from global insert overrides in other mods.
	override bool CanInsertItemInActualStorage(IEntity item, BaseInventoryStorageComponent storage, int slotID = -1)
	{
		ACM_ArsenalLoadoutRegistration.EnsureRegistered();

		if (!item || !storage)
			return false;

		SCR_WeaponAttachmentsStorageComponent weaponStorage = SCR_WeaponAttachmentsStorageComponent.Cast(storage);
		if (weaponStorage)
		{
			return ACM_CanValidateWeaponAttachmentStorage(item, weaponStorage, slotID);
		}

		bool ACMValidation = ACM_CanInsertItemInActualStorageInternal(item, storage, slotID);

		// Only ACM storages are logged; this hook fires constantly for UI hover checks.
		if (ACM_EquipmentStorageComponent.Cast(storage) || ACM_PouchStorageComponent.Cast(storage))
			ACM_ReplicationDiag.StepStorage("2.VALIDATE CanInsertItemInActualStorage", item, storage, slotID, string.Format("result=%1", ACMValidation));

		return ACMValidation;
	}

	protected bool ACM_CanInsertItemInActualStorageInternal(IEntity item, BaseInventoryStorageComponent storage, int slotID)
	{

		BaseInventoryStorageComponent routablePolicyStorage = ACM_GetRoutablePolicyStorageForDirectInsert(storage);
		if (routablePolicyStorage && ACM_IsPolicyRouteItem(item))
		{
			if (slotID >= 0 && routablePolicyStorage == storage)
			{
				ACM_EquipmentStorageSlot ACMSlot = ACM_EquipmentStorageSlot.Cast(storage.GetSlot(slotID));
				if (ACMSlot)
					return ACMSlot.ACM_CanAcceptItem(item);

				ACM_EquipmentStorageComponent ACMStorage = ACM_EquipmentStorageComponent.Cast(storage);
				if (ACMStorage)
					return ACMStorage.CanStoreItem(item, slotID);
			}

			ACM_StorageRouteCandidate policyCandidate = ACM_MagGrenadePolicyRouter.ResolveBestPolicyCandidateForStorage(routablePolicyStorage, item);
			return policyCandidate && policyCandidate.IsValid();
		}

		// Policy items must not be validated into generic/internal storage while a real
		// ACM policy slot is available. This prevents mags/smokes/frags from being
		// swallowed by vest/internal/container storage before their placard/pouch slot wins.
		if (ACM_ShouldBlockGenericPolicyStorageCandidate(item, storage))
			return false;

		return super.CanInsertItemInActualStorage(item, storage, slotID);
	}

	// Main Reforger insert hook. Build one request, let ACM handle managed routing when needed, otherwise fall back to the engine path.
	override void InsertItem(
		IEntity pItem,
		BaseInventoryStorageComponent pStorageTo = null,
		BaseInventoryStorageComponent pStorageFrom = null,
		SCR_InvCallBack cb = null)
	{
		ACM_ArsenalLoadoutRegistration.EnsureRegistered();

		ACM_ReplicationDiag.Step("1.INSERT InsertItem entry", pItem, string.Format("to=%1 from=%2 animReady=%3 locked=%4",
			ACM_ReplicationDiag.DescribeStorage(pStorageTo), ACM_ReplicationDiag.DescribeStorage(pStorageFrom), IsAnimationReady(), IsInventoryLocked()));

		if (!pItem || !IsAnimationReady() || IsInventoryLocked())
			return;

		SetInventoryLocked(true);


		   ACM_InventoryInsertRequest request = new ACM_InventoryInsertRequest(pItem, pStorageTo, pStorageFrom, cb);
		   request.CaptureManagedStorages(this);

		   // If the item is already attached to any inventory storage, treat this request as a MOVE
		   // even when BLE / UI fails to pass pStorageFrom. Without this, TryInsertItemInStorage can
		   // clone/duplicate the visual entity into the destination while leaving the old attached copy.
		   BaseInventoryStorageComponent currentItemStorage = pStorageFrom;
		   if (!currentItemStorage)
			   currentItemStorage = ACM_FindStorageContainingItem(pItem);

		   // Dragging a mag OUT of an ACM pouch/placard must be allowed to pass through to the engine.
		   // Do not auto-route it straight back into the same policy pouch.
		   bool sourceIsACMPolicyStorage = ACM_GetRoutablePolicyStorageForDirectInsert(currentItemStorage) != null;
		   bool destinationIsSameStorage = currentItemStorage && pStorageTo == currentItemStorage;
		   bool destinationIsACMPolicyStorage = ACM_GetRoutablePolicyStorageForDirectInsert(pStorageTo) != null;
		   bool movingOutOfACMPolicyStorage = sourceIsACMPolicyStorage && pStorageTo && !destinationIsSameStorage && !destinationIsACMPolicyStorage;

		   // Policy-based mag/grenade routing.
		   // Direct-adds into single pouches/placards target their actual ACM_Pocket/Equipment storage.
		   // We only route deposits INTO ACM policy storage, not removals OUT of it.
		   BaseInventoryStorageComponent directPolicyStorage = ACM_GetRoutablePolicyStorageForDirectInsert(pStorageTo);
		   bool itemIsPolicyItem = ACM_IsPolicyRouteItem(pItem);

		   // Respect explicit drops into non-ACM universal containers (worn IFAK / Map Pouch / backpack
		   // / external loot containers). Without this gate, dragging a magazine into a worn IFAK
		   // gets hijacked by ACM routing into a placard mag cell — the user sees the item "disappear"
		   // because it lands somewhere they didn't choose. ACM_PouchStorageComponent / ACM_EquipmentStorageComponent
		   // targets still go through ACM routing because directPolicyStorage resolves them.
		   bool targetIsForeignContainer = (pStorageTo != null)
			   && (directPolicyStorage == null)
			   && (SCR_UniversalInventoryStorageComponent.Cast(pStorageTo) != null);

		   // Policy route always gets first refusal for policy items.
			   // Bypasses: an explicit move OUT of an ACM policy storage (so users can remove
			   // mags/nades/radios from pouches without ACM immediately pulling them back), and
			   // an explicit drop INTO a non-ACM universal container (worn IFAK / backpack /
			   // loot crate) — previously computed but never applied, which caused the exact
			   // "item disappears into a placard" hijack the comment above describes.
			   bool shouldTryPolicyRoute = !movingOutOfACMPolicyStorage && !targetIsForeignContainer && itemIsPolicyItem;

		   if (shouldTryPolicyRoute)
		   {
			   ACM_StorageRouteCandidate policyCandidate = null;

			   if (directPolicyStorage)
				   policyCandidate = ACM_MagGrenadePolicyRouter.ResolveBestPolicyCandidateForStorage(directPolicyStorage, pItem);

			   bool directCandidateValid = policyCandidate && policyCandidate.IsValid();

			   // Always allow a global fallback when the directly targeted storage has no valid
			   // candidate. ACM routing must win over vanilla for every policy item, regardless
			   // of whether the UI targeted an ACM storage or some other (non-ACM) container.
			   if (!directCandidateValid)
			   {
				   ACM_StorageRouteCandidate globalCandidate = ACM_MagGrenadePolicyRouter.ResolveBestPolicyCandidate(this, pItem);
				   if (globalCandidate && globalCandidate.IsValid())
					   policyCandidate = globalCandidate;
			   }

			   if (ACM_MagGrenadePolicyRouter.ACM_DEBUG_ROUTING)
			   {
				   string itemName = "<no-prefab>";
				   EntityPrefabData itemPrefab = pItem.GetPrefabData();
				   if (itemPrefab)
					   itemName = itemPrefab.GetPrefabName();
				   ACM_EItemFamily fam = ACM_ItemClassificationService.ACM_ResolveItemFamily(pItem);
				   bool hasCandidate = policyCandidate && policyCandidate.IsValid();
				   int chosenScore = 0;
				   int chosenSlotIndex = -1;
				   if (hasCandidate)
				   {
					   chosenScore = policyCandidate.Score;
					   chosenSlotIndex = policyCandidate.SlotIndex;
				   }
				   Print(string.Format("[ACM Router] InsertItem item=%1 family=%2 directTarget=%3 chosen=%4 slotIdx=%5 score=%6",
					   itemName, fam, directPolicyStorage != null, hasCandidate, chosenSlotIndex, chosenScore), LogLevel.NORMAL);
			   }

			   if (policyCandidate && policyCandidate.IsValid())
			   {
				   bool routeAsMove = request.IsMoveRequest() || currentItemStorage != null;
				   ACM_ReplicationDiag.StepStorage("1a.ROUTE policy candidate chosen", pItem, policyCandidate.Storage, policyCandidate.SlotIndex,
					   string.Format("asMove=%1 score=%2", routeAsMove, policyCandidate.Score));
				   bool policyHandled = ACM_StorageRouteResolver.TryRouteToCandidate(this, pItem, policyCandidate, routeAsMove, cb);
				   ACM_ReplicationDiag.Step("1b.ROUTE request issued", pItem, string.Format("handled=%1 (server answer follows as SERVER-ACCEPTED/REJECTED)", policyHandled));
				   if (policyHandled)
				   {
					   ACM_PlayInvPickupSound();
					   SetInventoryLocked(false);
				   	   return;
				   }
			   }

			   // Compatibility path for engine/BLE loadout replay: when a concrete destination
			   // storage is explicitly provided but ACM routing has no candidate yet, allow
			   // vanilla insert fallback instead of hard-failing and dropping the item.
			   if (pStorageTo)
			   {
				   if (ACM_MagGrenadePolicyRouter.ACM_DEBUG_ROUTING)
				   {
					   Print("[ACM Router] No ACM candidate, explicit destination present; allowing vanilla fallback.", LogLevel.WARNING);
				   }

				   ACM_ReplicationDiag.StepStorage("1c.ROUTE no ACM candidate -> vanilla InsertItem", pItem, pStorageTo, -1);
				   SetInventoryLocked(false);
				   super.InsertItem(pItem, pStorageTo, pStorageFrom, cb);
				   return;
			   }

			   // Authoritative ACM path for unspecified destinations: never fall through
			   // to vanilla for policy items when no target was explicitly requested.
			   if (ACM_MagGrenadePolicyRouter.ACM_DEBUG_ROUTING)
			   {
				   Print("[ACM Router] No valid ACM candidate for policy item; vanilla fallback blocked.", LogLevel.WARNING);
			   }
			   ACM_ReplicationDiag.Step("1c.ROUTE no ACM candidate, no destination -> DROPPED (nothing inserted)", pItem);
			   SetInventoryLocked(false);
			   return;
		   }


		   ACM_EInsertDecision decision = ACM_InventoryInsertInterceptor.BuildInsertDecision(this, request);
		   bool handled = ACM_InventoryInsertInterceptor.TryHandleDecision(this, decision, request);
		   if (handled)
		   {
			   ACM_PlayInvPickupSound();
			   SetInventoryLocked(false);
			   return;
		   }

		ACM_ReplicationDiag.StepStorage("1c.ROUTE non-policy item -> vanilla InsertItem", pItem, pStorageTo, -1);
		SetInventoryLocked(false);
		super.InsertItem(pItem, pStorageTo, pStorageFrom, cb);
	}

	// Finds the storage that currently owns an attached item. Used as a safety net because
	// some UI paths do not pass pStorageFrom on drag/move.
	protected BaseInventoryStorageComponent ACM_FindStorageContainingItem(IEntity item)
	{
		if (!item)
			return null;

		array<BaseInventoryStorageComponent> storages = {};
		GetStorages(storages);

		foreach (BaseInventoryStorageComponent storage : storages)
		{
			if (!storage)
				continue;

			for (int slotIndex = 0; slotIndex < storage.GetSlotsCount(); slotIndex++)
			{
				InventoryStorageSlot slot = storage.GetSlot(slotIndex);
				if (!slot)
					continue;

				if (slot.GetAttachedEntity() == item)
					return storage;
			}
		}

		return null;
	}

	// BLE and some arsenal paths target the pocket/pouch inventory storage instead of the
	// equipment placement storage. Treat storages that directly host ACM visible slots as
	// policy destinations too so their logical occupancy groups are enforced before vanilla.
	protected BaseInventoryStorageComponent ACM_GetRoutablePolicyStorageForDirectInsert(BaseInventoryStorageComponent requestedStorage)
	{
		if (!requestedStorage)
			return null;

		ACM_EquipmentStorageComponent directPolicyStorage = ACM_EquipmentStorageComponent.Cast(requestedStorage);
		if (directPolicyStorage)
		{
			if (directPolicyStorage.ACM_IsPolicyDriven())
				return directPolicyStorage;

			return null;
		}

		if (ACM_HasEquipmentPolicySlots(requestedStorage))
			return requestedStorage;

		IEntity storageOwner = requestedStorage.GetOwner();
		if (!storageOwner)
			return null;

		ACM_EquipmentStorageComponent ownerPolicyStorage = ACM_EquipmentStorageComponent.Cast(storageOwner.FindComponent(ACM_EquipmentStorageComponent));
		if (ownerPolicyStorage && ownerPolicyStorage.ACM_IsPolicyDriven())
			return ownerPolicyStorage;

		return null;
	}

	// Blocks generic/internal storages from winning validation for policy items when ACM has a real policy slot available.
	// This is intentionally narrow: direct ACM policy storages are handled above, and generic storage is only blocked
	// when a valid ACM placard/pouch candidate exists somewhere in the inventory graph.
	protected bool ACM_ShouldBlockGenericPolicyStorageCandidate(IEntity item, BaseInventoryStorageComponent storage)
	{
		if (!item || !storage)
			return false;

		if (!ACM_IsPolicyRouteItem(item))
			return false;

		// Never block the actual ACM policy storage path here; those storages validate through slot policy.
		if (ACM_GetRoutablePolicyStorageForDirectInsert(storage))
			return false;

		// Generic/internal storage families that otherwise tend to win vanilla FindStorageForInsert.
		bool isGenericStorage = SCR_UniversalInventoryStorageComponent.Cast(storage) != null
			|| ACM_ClothNodeStorageComponent.Cast(storage) != null
			|| ClothNodeStorageComponent.Cast(storage) != null;

		if (!isGenericStorage)
			return false;

		ACM_StorageRouteCandidate policyCandidate = ACM_MagGrenadePolicyRouter.ResolveBestPolicyCandidate(this, item);
		return policyCandidate && policyCandidate.IsValid();
	}

	protected bool ACM_HasEquipmentPolicySlots(BaseInventoryStorageComponent storage)
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

	protected bool ACM_IsPolicyRouteItem(IEntity item)
	{
		if (!item)
			return false;

		// Resolve family once instead of making 11 separate ItemMatchesCommonType calls,
		// each of which rebuilds the type array independently.
		ACM_EItemFamily family = ACM_ItemClassificationService.ACM_ResolveItemFamily(item);
		if (family != ACM_EItemFamily.NONE)
			return true;

		// Weapon-type fallback: radios and throwables that only carry WT_ tags or live
		// SCR_RadioComponent identity without an ACM common item type still need routing.
		if (ACM_ItemClassificationService.IsRadio(item))
			return true;

		if (ACM_ItemClassificationService.MatchesWeaponType(item, EWeaponType.WT_SMOKEGRENADE)
			|| ACM_ItemClassificationService.MatchesWeaponType(item, EWeaponType.WT_FRAGGRENADE))
			return true;

		return false;
	}

	// Mirrors the default pickup feedback only when ACM completed the insert itself.
	protected void ACM_PlayInvPickupSound()
	{
		if (m_bACM_SuppressFeedback)
			return;

		SCR_UISoundEntity.SoundEvent(SCR_SoundEvent.SOUND_INV_CONTAINER_DIFR_DROP);
		if (m_CharacterController)
			m_CharacterController.TryPlayItemGesture(EItemGesture.EItemGesturePickUp);
	}

	protected bool ACM_CanValidateWeaponAttachmentStorage(
		IEntity item,
		SCR_WeaponAttachmentsStorageComponent weaponStorage,
		int slotID)
	{
		if (!item || !weaponStorage)
			return false;

		if (slotID >= 0)
		{
			return weaponStorage.CanStoreItem(item, slotID);
		}

		int feedSlot = ACM_WeaponFeedService.FindFeedSlotIndex(weaponStorage, item);
		return feedSlot >= 0;
	}
}
