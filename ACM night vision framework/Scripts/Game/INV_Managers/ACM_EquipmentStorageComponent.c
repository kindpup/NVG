//  ACM
//  ACM Equipment Storage Component
//
//  Original ACM-owned script for the modular framework.
//==================================================================================================

class ACM_EquipmentStorageComponentClass : SCR_EquipmentStorageComponentClass {}

class ACM_EquipmentStorageComponent : SCR_EquipmentStorageComponent
{
	[Attribute(defvalue: "false", desc: "Enable policy-driven mag/grenade routing for this storage")] 
	protected bool m_bACM_PolicyDriven;

	bool ACM_IsPolicyDriven() { return m_bACM_PolicyDriven; }
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		ACM_ArsenalLoadoutRegistration.EnsureRegistered();

		// Step 0: the pouch entity itself exists on this machine (Character -> Belt/Vest -> Pouch).
		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().CallLater(ACM_DiagLogPouchSpawn, 500, false);
	}

	protected void ACM_DiagLogPouchSpawn()
	{
		IEntity owner = GetOwner();
		if (!owner)
			return;

		string parentName = "<no-parent>";
		if (owner.GetParent())
			parentName = ACM_ReplicationDiag.PrefabOf(owner.GetParent());

		ACM_ReplicationDiag.Step("0.POUCH present", owner, string.Format("storageClass=%1 parent=%2 slots=%3", ClassName(), parentName, GetSlotsCount()));
	}

	override protected bool ShouldPreviewAttachedItems()
	{
		return true;
	}

	event override bool OnOverrideCanStoreItem()
	{
		return true;
	}

	event override bool OnOverrideCanReplaceItem()
	{
		return true;
	}

	void TryRefillVisibleSlotFromInventory(ACM_EquipmentStorageSlot visibleSlot)
	{
		// During serialized loadout replay, do not pull reserve items into a
		// visible slot while vanilla/BLE is still rebuilding the saved graph.
		if (ACM_LoadoutReplayCompat.IsReplayWindowActive())
			return;

		ACM_ReserveRefillService.TryRefillSlot(visibleSlot);
	}

	override bool CanStoreItem(IEntity item, int slotID)
	{
		if (!item)
			return false;

		if (slotID > -1)
		{
			ACM_LoadoutReplayCompat.MarkReplayWindow();
			bool canStore = InternalCanStore(item, GetSlot(slotID), true);
			ACM_ReplicationDiag.StepStorage("2a.VALIDATE storage.CanStoreItem(explicit slot)", item, this, slotID, string.Format("result=%1", canStore));
			return canStore;
		}

		for (int i = 0; i < GetSlotsCount(); i++)
		{
			if (InternalCanStore(item, GetSlot(i), false))
				return true;
		}

		return false;
	}

	protected bool InternalCanStore(IEntity item, InventoryStorageSlot slot, bool explicitSlotRequest = false)
	{
		SCR_EquipmentStorageSlot ruleSlot = SCR_EquipmentStorageSlot.Cast(slot);
		if (!ruleSlot)
			return false;

		ACM_EquipmentStorageSlot ACMSlot = ACM_EquipmentStorageSlot.Cast(slot);

		// Explicit slot restore should keep the serialized slot graph from BLE/vanilla,
		// but it must still respect the slot's item policy. Only logical occupancy
		// groups are bypassed here; normal auto-routing keeps full ACM enforcement.
		if (explicitSlotRequest)
		{
			if (ACMSlot && ACM_IsOccupancyBlocked(ACMSlot, item))
				return false;

			return ruleSlot.ACM_CanAcceptExplicitReplayItem(item);
		}

		if (ACMSlot && ACM_IsOccupancyBlocked(ACMSlot))
			return false;

		return ruleSlot.ACM_CanAcceptItem(item);
	}

	// Enforces logical occupancy groups at the storage component level.
	// This catches normal inventory insert/drag/drop paths, not just ACM policy router candidates.
	protected bool ACM_IsOccupancyBlocked(ACM_EquipmentStorageSlot candidateSlot, IEntity movingItem = null)
	{
		if (!candidateSlot)
			return true;

		IEntity attachedEntity = candidateSlot.GetAttachedEntity();
		if (attachedEntity && attachedEntity != movingItem)
			return true;

		string group = candidateSlot.ACM_GetOccupancyGroup();
		if (group.IsEmpty() || group == "NONE")
			return false;

		for (int i = 0; i < GetSlotsCount(); i++)
		{
			InventoryStorageSlot slot = GetSlot(i);
			if (!slot)
				continue;

			ACM_EquipmentStorageSlot ACMSlot = ACM_EquipmentStorageSlot.Cast(slot);
			if (!ACMSlot)
				continue;

			if (ACMSlot == candidateSlot)
				continue;

			if (ACMSlot.ACM_GetOccupancyGroup() != group)
				continue;

			IEntity otherAttached = ACMSlot.GetAttachedEntity();
			if (otherAttached && otherAttached != movingItem)
				return true;
		}

		return false;
	}

	override bool CanReplaceItem(IEntity nextItem, int slotID)
	{
		if (!nextItem)
			return false;

		if (slotID > -1)
		{
			ACM_LoadoutReplayCompat.MarkReplayWindow();
			return InternalCanReplace(nextItem, GetSlot(slotID), true);
		}

		for (int i = 0; i < GetSlotsCount(); i++)
		{
			if (InternalCanReplace(nextItem, GetSlot(i), false))
				return true;
		}

		return false;
	}

	protected bool InternalCanReplace(IEntity nextItem, InventoryStorageSlot slot, bool explicitSlotRequest = false)
	{
		SCR_EquipmentStorageSlot ruleSlot = SCR_EquipmentStorageSlot.Cast(slot);
		if (!ruleSlot)
			return false;

		ACM_EquipmentStorageSlot ACMSlot = ACM_EquipmentStorageSlot.Cast(slot);

		if (explicitSlotRequest)
		{
			if (ACMSlot && ACM_IsOccupancyBlocked(ACMSlot, nextItem))
				return false;

			return ruleSlot.ACM_CanAcceptExplicitReplayItem(nextItem);
		}

		return ruleSlot.ACM_CanReplaceWith(nextItem);
	}
}

class ACM_PocketStorageComponentClass : ACM_EquipmentStorageComponentClass {}
// Legacy prefab alias for historical pocket entities.
// Keep this class so existing prefabs and saved loadouts continue resolving,
// but do not add behavior here; ACM_EquipmentStorageComponent is the single
// behavior owner for both Equipment and Pocket storage variants.
class ACM_PocketStorageComponent : ACM_EquipmentStorageComponent {}
