//
//  ACM
//  ACM Equipment Storage Slot
//
//  Original ACM-owned script for the modular framework.
//==================================================================================================

//------------------------------------------------------------------------------------------------
// Workbench dropdown presets for ACM policy-driven magazine slots.
// These are stored as strings so the router can compare them directly.
enum ACM_ESlotIDPreset
{
	NONE = 0,
	Placard_Cell_01,
	Placard_Cell_02,
	Placard_Cell_03,
	Placard_Cell_04,
	Placard_Cell_05,
	Placard_Cell_06,
	Placard_Cell_07,
	Placard_Cell_08,
	Placard_Cell_09,
	Placard_Cell_10,
	Belt_Cell_01,
	Belt_Cell_02,
	Belt_Cell_03,
	Belt_Cell_04,
	Chest_Left_01,
	Chest_Left_02,
	Chest_Right_01,
	Chest_Right_02,
	additonal_1,
	additonal_2,
	additonal_3,
	additonal_4,
	additonal_5,
	additonal_6
}

// Keep this separate from Slot ID so you can have multiple visual variants
// share one logical physical space.
enum ACM_EOccupancyGroupPreset
{
	NONE = 0,
	Placard_Cell_01,
	Placard_Cell_02,
	Placard_Cell_03,
	Placard_Cell_04,
	Placard_Cell_05,
	Placard_Cell_06,
	Placard_Cell_07,
	Placard_Cell_08,
	Placard_Cell_09,
	Placard_Cell_10,
	Belt_Cell_01,
	Belt_Cell_02,
	Belt_Cell_03,
	Belt_Cell_04,
	Chest_Left_01,
	Chest_Left_02,
	Chest_Right_01,
	Chest_Right_02,
	additonal_1,
	additonal_2,
	additonal_3,
	additonal_4,
	additonal_5,
	additonal_6
}

class ACM_EquipmentStorageSlot : SCR_EquipmentStorageSlot
{
	[Attribute(ECommonItemType.NONE.ToString(), UIWidgets.ComboBox,
		"Restrict this slot to a specific mag/grenade category. Use NONE for generic fallback.", enums: ParamEnumArray.FromEnum(ECommonItemType))]
	protected string m_ACM_MagCategory;

	[Attribute(ACM_ESlotIDPreset.NONE.ToString(), UIWidgets.ComboBox,
		"ACM Slot ID for policy-based placement / diagnostics. Usually unique per visual slot variant.", enums: ParamEnumArray.FromEnum(ACM_ESlotIDPreset))]
	protected string m_ACM_SlotID;

	[Attribute(defvalue: "", desc: "Exact prefab/resource name this slot should accept first. Leave empty to use ACM Mag Category / Allowed Types fallback.")]
	protected ResourceName m_ACM_ItemPrefab;

	[Attribute(ACM_EOccupancyGroupPreset.NONE.ToString(), UIWidgets.ComboBox,
		"Logical occupancy group for mutually-exclusive physical positions. Slots with the same group block each other.", enums: ParamEnumArray.FromEnum(ACM_EOccupancyGroupPreset))]
	protected string m_ACM_OccupancyGroup;

	string ACM_GetSlotID()
	{
		return m_ACM_SlotID;
	}

	string ACM_GetItemPrefabRule()
	{
		return m_ACM_ItemPrefab;
	}

	string ACM_GetOccupancyGroup()
	{
		// Unedited combo-box attributes serialize the enum's numeric default ("0"),
		// not the label "NONE". Without this normalization every slot left at the
		// default shares the literal group "0" and they all block each other.
		if (m_ACM_OccupancyGroup.IsEmpty() || m_ACM_OccupancyGroup == "0" || m_ACM_OccupancyGroup == "NONE")
			return "NONE";

		return m_ACM_OccupancyGroup;
	}

	protected string ACM_GetEntityPrefabName(IEntity item)
	{
		if (!item)
			return string.Empty;

		EntityPrefabData prefabData = item.GetPrefabData();
		if (!prefabData)
			return string.Empty;

		return prefabData.GetPrefabName();
	}

	protected bool ACM_DoesPrefabMatch(IEntity item)
	{
		if (m_ACM_ItemPrefab.IsEmpty())
			return true;

		string itemPrefab = ACM_GetEntityPrefabName(item);
		if (itemPrefab.IsEmpty())
			return false;

		return itemPrefab == m_ACM_ItemPrefab;
	}

	bool ACM_MatchesPolicyItem(IEntity item)
	{
		// Single source of truth for slot/item compatibility. Avoids a side path that
		// could let a utility item match an ammo slot via raw common-type checks.
		return IsItemCompatible(item);
	}

	// Explicit slot replay is only allowed to bypass logical occupancy groups.
	// It still uses the normal ACM item policy/category checks so routing does
	// not become permissive after BLE/vanilla loadout restore.
	override bool ACM_CanAcceptExplicitReplayItem(IEntity item)
	{
		ACM_LoadoutReplayCompat.MarkReplayWindow();

		if (!item)
			return false;

		IEntity attached = GetAttachedEntity();
		if (attached && attached != item)
			return false;

		return IsItemCompatible(item);
	}

	override bool ACM_CanAcceptExplicitReplayResource(ResourceName resourceName)
	{
		ACM_LoadoutReplayCompat.MarkReplayWindow();

		if (GetAttachedEntity())
			return false;

		return IsResourceCompatible(resourceName);
	}


	// Hard slot-level occupancy enforcement.
	// This catches direct slot/Workbench/BLE paths that can bypass ACM_EquipmentStorageComponent.CanStoreItem().
	bool ACM_IsOccupiedOrGroupBlocked()
	{
		if (GetAttachedEntity())
			return true;

		string group = ACM_GetOccupancyGroup();
		if (group.IsEmpty() || group == "NONE")
			return false;

		BaseInventoryStorageComponent storage = GetStorage();
		if (!storage)
			return false;

		for (int slotIndex = 0; slotIndex < storage.GetSlotsCount(); slotIndex++)
		{
			InventoryStorageSlot otherSlot = storage.GetSlot(slotIndex);
			if (!otherSlot)
				continue;

			ACM_EquipmentStorageSlot otherACMSlot = ACM_EquipmentStorageSlot.Cast(otherSlot);
			if (!otherACMSlot)
				continue;

			if (otherACMSlot == this)
				continue;

			if (otherACMSlot.ACM_GetOccupancyGroup() != group)
				continue;

			if (otherSlot.GetAttachedEntity())
				return true;
		}

		return false;
	}

	// Returns the normalized category for this slot, or NONE if not set
	ECommonItemType ACM_GetSlotMagCategory()
	{
		if (m_ACM_MagCategory.IsEmpty() || m_ACM_MagCategory == "NONE")
			return ECommonItemType.NONE;
		string normalizedTypeName = ACM_ItemClassificationService.NormalizeCommonItemTypeName(m_ACM_MagCategory);
		int parsedType = typename.StringToEnum(ECommonItemType, normalizedTypeName);
		if (parsedType < 0)
			return ECommonItemType.NONE;
		return parsedType;
	}
	protected int m_iInteractionLayer;

	bool CanStoreItem(IEntity item)
	{
		return !ACM_IsOccupiedOrGroupBlocked() && IsItemCompatible(item);
	}

	override bool ACM_CanAcceptItem(IEntity item)
	{
		return CanStoreItem(item);
	}

	bool ACM_CanStoreResource(ResourceName resourceName)
	{
		return !ACM_IsOccupiedOrGroupBlocked() && IsResourceCompatible(resourceName);
	}

	override bool ACM_CanAcceptResource(ResourceName resourceName)
	{
		return ACM_CanStoreResource(resourceName);
	}

	bool CanReplaceItem(IEntity nextItem)
	{
		return IsItemCompatible(nextItem);
	}

	override bool ACM_CanReplaceWith(IEntity nextItem)
	{
		return CanReplaceItem(nextItem);
	}

	protected bool IsItemCompatible(IEntity item)
	{
		if (!item)
			return false;

		if (!InventoryItemComponent.Cast(item.FindComponent(InventoryItemComponent)))
			return false;

		if (!ACM_DoesPrefabMatch(item))
			return false;

		ECommonItemType slotCategory = ACM_GetSlotMagCategory();
		ACM_EItemFamily slotFamily = ACM_ItemClassificationService.ACM_SlotCategoryToFamily(slotCategory);
		ACM_EItemFamily itemFamily = ACM_ItemClassificationService.ACM_ResolveItemFamily(item);

		// NONE slot category is the legacy "any magazine" fallback used by older placards.
		// Treat it as the AMMO family so utility items (smoke / frag / radio) cannot land here.
		if (slotFamily == ACM_EItemFamily.NONE)
			slotFamily = ACM_EItemFamily.AMMO;

		if (itemFamily != slotFamily)
			return false;

		// Defensive: if this is an AUTO family slot but no specific mag-category (sub-type)
		// is configured and no explicit prefab rule is set, treat it as non-accepting to
		// avoid permissively accepting all AUTO items (headsets, NVGs, etc.).
		if (slotFamily == ACM_EItemFamily.AUTO && slotCategory == ECommonItemType.NONE && m_ACM_ItemPrefab.IsEmpty())
			return false;

		// Ammo cells must additionally match the configured magazine sub-category when set.
		if (slotFamily == ACM_EItemFamily.AMMO && slotCategory != ECommonItemType.NONE)
			return ACM_ItemClassificationService.ItemMatchesCommonType(item, slotCategory);

		// Auto-routed cells (headsets, NVGs, RailLites, ...) share one family but differentiate
		// position via slot's m_ACM_MagCategory (e.g. ACM_EAR_LEFT vs ACM_EAR_RIGHT).
		if (slotFamily == ACM_EItemFamily.AUTO && slotCategory != ECommonItemType.NONE)
			return ACM_ItemClassificationService.ItemMatchesCommonType(item, slotCategory);

		return true;
	}

	protected bool IsResourceCompatible(ResourceName resourceName)
	{
		if (resourceName.IsEmpty())
			return false;

		if (!m_ACM_ItemPrefab.IsEmpty() && resourceName != m_ACM_ItemPrefab)
			return false;

		ECommonItemType slotCategory = ACM_GetSlotMagCategory();
		ACM_EItemFamily slotFamily = ACM_ItemClassificationService.ACM_SlotCategoryToFamily(slotCategory);
		ACM_EItemFamily resourceFamily = ACM_ItemClassificationService.ACM_ResolveResourceFamily(resourceName);

		if (slotFamily == ACM_EItemFamily.NONE)
			slotFamily = ACM_EItemFamily.AMMO;

		if (resourceFamily != slotFamily)
			return false;

		// Defensive: avoid accepting any AUTO resource when no sub-type or prefab is specified.
		if (slotFamily == ACM_EItemFamily.AUTO && slotCategory == ECommonItemType.NONE && m_ACM_ItemPrefab.IsEmpty())
			return false;

		if (slotFamily == ACM_EItemFamily.AMMO && slotCategory != ECommonItemType.NONE)
			return ACM_ItemClassificationService.ResourceMatchesCommonType(resourceName, slotCategory);

		if (slotFamily == ACM_EItemFamily.AUTO && slotCategory != ECommonItemType.NONE)
			return ACM_ItemClassificationService.ResourceMatchesCommonType(resourceName, slotCategory);

		return true;
	}

	override void OnAttachedEntity(IEntity entity)
	{
		super.OnAttachedEntity(entity);
		ACM_ReplicationDiag.LogAttach(this, entity);
		ACM_OnStoredItemAttached(entity);

		// Apply ACM visual placement offset/rotation after the engine attaches the item.
		// Deferred one tick because the engine resets the item's local transform during attach;
		// applying inline gets overwritten and the placement Offset/Angles are ignored.
		GetGame().GetCallqueue().CallLater(ACM_ApplyPlacementTransform, 0, false, entity);

		// Defense-in-depth: external paths (loadout editor, Bacon UI, future mods) can attach
		// items directly to a slot without going through our InsertItem hook. If this slot's
		// IsItemCompatible rule rejects the attached item, push it back through the inventory
		// manager so ACM routing relocates it to the correct cell. During serialized loadout
		// replay, suppress the deferred correction only; do not disable normal routing.
		//
		// REPLICATION: this callback fires on every machine (server, owner, remote proxies).
		// Only the authority may initiate the corrective move — a proxy issuing InsertItem
		// against a replicated attach desyncs its local slot graph from the server (item
		// visible on one machine, missing on the others).
		if (!ACM_LoadoutReplayCompat.IsReplayWindowActive()
			&& ACM_ReplicationAuthority.HasAuthorityOver(entity))
			GetGame().GetCallqueue().CallLater(ACM_TryFamilyRebound, 0, false, entity);
	}

	override void OnDetachedEntity(IEntity entity)
	{
		super.OnDetachedEntity(entity);
		ACM_ReplicationDiag.LogDetach(this, entity);

		if (!entity)
			return;

		// Parity fix: attach suspends the stored item's physics interaction layer, so
		// detach must restore it or items removed from visible slots keep interaction
		// layer 0 (no collision, cannot be targeted on the ground).
		ACM_StoredItemLifecycleService.RestoreStoredItemPhysics(entity, m_iInteractionLayer);

		if (ACM_LoadoutReplayCompat.IsReplayWindowActive())
			return;

		// REPLICATION: this callback fires on every machine. Only the authority may
		// initiate the reserve refill; proxies must wait for the replicated result.
		if (!ACM_ReplicationAuthority.HasAuthorityOver(GetOwner()))
			return;

		SCR_ExplosiveChargeInventoryItemComponent explosiveCharge = SCR_ExplosiveChargeInventoryItemComponent.Cast(entity.FindComponent(SCR_ExplosiveChargeInventoryItemComponent));
		ACM_EquipmentStorageComponent ACMStorage = ACM_EquipmentStorageComponent.Cast(GetStorage());
		if (ACMStorage && !explosiveCharge)
		{
			// Deferred one tick: the engine is still mid-detach inside this callback;
			// mutating other storages synchronously here re-enters inventory code and
			// can corrupt bulk operations (loadout clear, death gear transfer).
			GetGame().GetCallqueue().CallLater(ACM_DeferredRefillFromStorage, 0, false);
		}
	}

	// Deferred continuation of OnDetachedEntity: re-resolves the storage at fire time
	// because the slot may have been re-parented or deleted during the elapsed tick.
	protected void ACM_DeferredRefillFromStorage()
	{
		ACM_EquipmentStorageComponent ACMStorage = ACM_EquipmentStorageComponent.Cast(GetStorage());
		if (ACMStorage)
		{
			ACM_ReplicationDiag.Step("6.REFILL visible slot emptied -> TryRefillVisibleSlotFromInventory", null, string.Format("slot=%1 storage=%2", m_ACM_SlotID, ACM_ReplicationDiag.DescribeStorage(ACMStorage)));
			ACMStorage.TryRefillVisibleSlotFromInventory(this);
		}
	}

	protected void ACM_OnStoredItemAttached(IEntity entity)
	{
		if (!entity)
			return;

		ACM_StoredItemLifecycleService.SuspendStoredItemPhysics(entity, m_iInteractionLayer);
	}

	protected void ACM_ApplyPlacementTransform(IEntity entity)
	{
		if (!entity || m_ACM_SlotID.IsEmpty())
			return;

		BaseInventoryStorageComponent storage = GetStorage();
		if (!storage)
			return;

		IEntity storageOwner = storage.GetOwner();
		if (!storageOwner)
			return;

		ACM_MagPlacementComponent placementComponent = ACM_MagPlacementComponent.Cast(storageOwner.FindComponent(ACM_MagPlacementComponent));
		if (!placementComponent)
			return;

		ACM_MagPlacementData placement = placementComponent.GetPlacementForItemAndSlot(entity, m_ACM_SlotID);
		if (!placement)
			return;

		vector localTransform[4];
		Math3D.AnglesToMatrix(placement.Angles, localTransform);
		localTransform[3] = placement.Offset;

		entity.SetLocalTransform(localTransform);
	}

	// Public hook so parent morph handlers can force this slot to recook the
	// attached entity placement offset/angles after the carrier shape changes.
	void ACM_RefreshAttachedVisualPlacement()
	{
		IEntity attachedEntity = GetAttachedEntity();
		if (!attachedEntity)
			return;

		ACM_ApplyPlacementTransform(attachedEntity);
		GetGame().GetCallqueue().CallLater(ACM_ApplyPlacementTransform, 0, false, attachedEntity);
	}

	// Toggle to log family-rebound diagnostics. Leave false in shipping builds.
	static const bool ACM_DEBUG_REBOUND = false;

	// Reroutes a misrouted item via the inventory manager so ACM routing chooses the correct
	// slot. Triggered when an external code path (loadout editor / Bacon UI) attaches an item
	// directly to a slot that this slot's IsItemCompatible rule would reject. Covers both
	// cross-family mismatches (radio in mag cell) and same-family sub-category mismatches
	// (rifle mag in a pistol-ammo cell).
	protected void ACM_TryFamilyRebound(IEntity entity)
	{
		if (ACM_LoadoutReplayCompat.IsReplayWindowActive())
			return;

		if (!entity)
			return;

		// Defense-in-depth authority guard (see OnAttachedEntity): re-checked here
		// because ownership/authority may have changed between scheduling and this
		// deferred tick, and other callers may reuse this method.
		if (!ACM_ReplicationAuthority.HasAuthorityOver(entity))
			return;

		// Item must still be attached to THIS slot. If it has already moved, do nothing.
		if (GetAttachedEntity() != entity)
			return;

		ACM_EItemFamily itemFamily = ACM_ItemClassificationService.ACM_ResolveItemFamily(entity);
		if (itemFamily == ACM_EItemFamily.NONE)
			return;

		// Use the same compatibility rule the slot enforces for normal accept paths. If this
		// rule says the item belongs here, we leave it alone. If it says the item does NOT
		// belong, reroute via the inventory manager so ACM routing picks the correct cell.
		if (IsItemCompatible(entity))
			return;

		BaseInventoryStorageComponent storage = GetStorage();
		if (!storage)
			return;

		IEntity storageOwner = storage.GetOwner();
		if (!storageOwner)
			return;

		IEntity rootOwner = storageOwner.GetRootParent();
		if (!rootOwner)
			return;

		SCR_InventoryStorageManagerComponent inventoryManager = SCR_InventoryStorageManagerComponent.Cast(rootOwner.FindComponent(SCR_InventoryStorageManagerComponent));
		if (!inventoryManager)
			return;

		// Avoid nested InsertItem calls while the manager is already locked by an
		// engine/BLE restore or another inventory operation. Normal live inserts
		// still route through SCR_InventoryStorageManagerComponent.InsertItem.
		if (inventoryManager.ACM_IsInventoryCurrentlyLocked())
			return;

		if (ACM_DEBUG_REBOUND)
		{
			string itemPrefab = "<no-prefab>";
			EntityPrefabData prefabData = entity.GetPrefabData();
			if (prefabData)
				itemPrefab = prefabData.GetPrefabName();
			ECommonItemType slotCategory = ACM_GetSlotMagCategory();
			Print(string.Format("[ACM Rebound] Incompatible attach detected. item=%1 itemFamily=%2 slotID=%3 cat=%4. Triggering reroute.",
				itemPrefab, itemFamily, m_ACM_SlotID, slotCategory), LogLevel.WARNING);
		}

		// Safety guard for external loadout replays (for example Bacon/engine apply paths):
		// only rebound when ACM routing can resolve a concrete candidate. If none exists,
		// keep the currently attached item instead of risking a failed reroute/drop.
		ACM_StorageRouteCandidate candidate = ACM_MagGrenadePolicyRouter.ResolveBestPolicyCandidate(inventoryManager, entity);
		if (!candidate || !candidate.IsValid())
		{
			if (ACM_DEBUG_REBOUND)
				Print("[ACM Rebound] No valid ACM candidate; keeping current attachment.", LogLevel.WARNING);
			return;
		}

		// Pass the item back through InsertItem with no explicit destination. Our modded hook
		// detects that the item is already attached and treats this as a move, scoring all ACM
		// candidates globally and routing to the correct family slot. The TryRouteToCandidate
		// family gate prevents this slot from winning again.
		ACM_ReplicationDiag.Step("6.REBOUND incompatible item -> re-route via InsertItem", entity, string.Format("fromSlot=%1", m_ACM_SlotID));
		inventoryManager.InsertItem(entity, null, storage, null);
	}
}

class ACM_VisibleSlot : ACM_EquipmentStorageSlot {}
class ACM_PocketSlot : ACM_EquipmentStorageSlot {}

class ACM_AccessorySlot : SCR_EquipmentStorageSlot
{
	[Attribute(ECommonItemType.NONE.ToString(), UIWidgets.ComboBox,
		"Compatible Common Item types for this slot. All if empty.", enums: ParamEnumArray.FromEnum(ECommonItemType))]
	protected ref array<string> m_AllowedTypeStrings;

	[Attribute(ECommonItemType.NONE.ToString(), UIWidgets.ComboBox,
		"Legacy allowed item types for this slot.", enums: ParamEnumArray.FromEnum(ECommonItemType))]
	protected ref array<string> m_aAllowedTypeStrings;

	[Attribute(ECommonItemType.NONE.ToString(), UIWidgets.ComboBox,
		"Legacy allowed item types for this slot.", enums: ParamEnumArray.FromEnum(ECommonItemType))]
	protected ref array<string> m_aAllowedTypeNames;

	protected ref array<ECommonItemType> m_AllowedTypes = {};
	protected int m_iInteractionLayer;

	protected void ACM_BuildAllowedTypeCache()
	{
		if (!m_AllowedTypes)
			m_AllowedTypes = {};

		if (!m_AllowedTypes.IsEmpty())
			return;

		ACM_StoragePolicy.AppendParsedTypes(m_AllowedTypeStrings, m_AllowedTypes);
		ACM_StoragePolicy.AppendParsedTypes(m_aAllowedTypeStrings, m_AllowedTypes);
		ACM_StoragePolicy.AppendParsedTypes(m_aAllowedTypeNames, m_AllowedTypes);
	}

	bool CanStoreItem(IEntity item)
	{
		return !GetAttachedEntity() && IsItemCompatible(item);
	}

	override bool ACM_CanAcceptItem(IEntity item)
	{
		return CanStoreItem(item);
	}

	bool ACM_CanStoreResource(ResourceName resourceName)
	{
		return !GetAttachedEntity() && IsResourceCompatible(resourceName);
	}

	override bool ACM_CanAcceptResource(ResourceName resourceName)
	{
		return ACM_CanStoreResource(resourceName);
	}

	bool CanReplaceItem(IEntity nextItem)
	{
		return IsItemCompatible(nextItem);
	}

	override bool ACM_CanReplaceWith(IEntity nextItem)
	{
		return CanReplaceItem(nextItem);
	}

	protected bool IsItemCompatible(IEntity item)
	{
		if (!ACM_ItemClassificationService.IsAccessoryItem(item) || !ACM_ItemClassificationService.IsChestAccessory(item))
			return false;

		ACM_BuildAllowedTypeCache();
		if (!m_AllowedTypes.Count())
			return true;

		if (ACM_StoragePolicy.DoesItemMatchAllowedTypes(item, m_AllowedTypes))
			return true;

		// ATAK fallback: only applies when the ITEM is actually an ATAK device.
		// (Previously this only checked whether the slot's allowed list contained
		// ACM_ATAK, which made such slots accept every chest accessory.)
		if (!ACM_ItemClassificationService.ItemMatchesCommonType(item, ECommonItemType.ACM_ATAK))
			return false;

		return ACM_StoragePolicy.IsItemTypeInAllowedList(ECommonItemType.ACM_ATAK, m_AllowedTypes);
	}

	protected bool IsResourceCompatible(ResourceName resourceName)
	{
		if (!ACM_ItemClassificationService.IsChestAccessoryResource(resourceName))
			return false;

		ACM_BuildAllowedTypeCache();
		if (!m_AllowedTypes.Count())
			return true;

		if (ACM_StoragePolicy.DoesResourceMatchAllowedTypes(resourceName, m_AllowedTypes))
			return true;

		// ATAK fallback: only applies when the RESOURCE is actually an ATAK device.
		if (!ACM_ItemClassificationService.ResourceMatchesCommonType(resourceName, ECommonItemType.ACM_ATAK))
			return false;

		return ACM_StoragePolicy.IsItemTypeInAllowedList(ECommonItemType.ACM_ATAK, m_AllowedTypes);
	}

	override void OnAttachedEntity(IEntity entity)
	{
		super.OnAttachedEntity(entity);
		ACM_ReplicationDiag.LogAttach(this, entity);
		ACM_OnStoredItemAttached(entity);
	}

	override void OnDetachedEntity(IEntity entity)
	{
		super.OnDetachedEntity(entity);
		ACM_ReplicationDiag.LogDetach(this, entity);
		ACM_OnStoredItemDetached(entity);
	}

	protected void ACM_OnStoredItemAttached(IEntity entity)
	{
		if (!entity)
			return;

		ACM_StoredItemLifecycleService.SuspendStoredItemPhysics(entity, m_iInteractionLayer);
	}

	protected void ACM_OnStoredItemDetached(IEntity entity)
	{
		if (!entity)
			return;

		ACM_StoredItemLifecycleService.RestoreStoredItemPhysics(entity, m_iInteractionLayer);
	}
}
