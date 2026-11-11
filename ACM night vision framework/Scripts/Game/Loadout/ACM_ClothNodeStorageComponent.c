//
//  ACM
//  ACM Cloth Node Storage Component
//
//  Original ACM-owned script for the modular framework.
//==================================================================================================

/*!
	Subclass of engine `ClothNodeStorageComponent` that opens its storage gate
	and adds a script-side max-weight cap (the engine class only exposes
	`CapacityCoefficient`; it has no `m_fMaxWeight` field of its own).

	The engine class cannot be `modded` (validation refuses
	`modded class ClothNodeStorageComponent`), so we subclass instead.

	Workbench authoring:
	  - Replace `ClothNodeStorageComponent` with `ACM_ClothNodeStorageComponent`
	    on the cloth prefab.
	  - Configure `MaxCumulativeVolume`, `m_Size`, `StoragePurpose`, etc. as
	    usual.
	  - Set "Max Weight" (`m_fACM_MaxWeight`) for an additional weight cap.
	    A value of `0` or less disables the cap.
*/

class ACM_ClothNodeStorageComponentClass : ClothNodeStorageComponentClass {}

class ACM_ClothNodeStorageComponent : ClothNodeStorageComponent
{
	[Attribute(defvalue: "0", desc: "Maximum cumulative weight (kg) of items stored in this cloth-node container. 0 or negative disables the cap.")]
	protected float m_fACM_MaxWeight;

	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		ACM_ArsenalLoadoutRegistration.EnsureRegistered();
	}

	// Generic acceptance \u2014 leaves slot-level / item-level rules in charge.
	override event bool OnOverrideCanStoreItem()
	{
		return true;
	}

	override event bool OnOverrideCanReplaceItem()
	{
		return true;
	}

	// Adds the weight cap on top of the engine acceptance check.
	override bool CanStoreItem(IEntity item, int slotID)
	{
		if (!super.CanStoreItem(item, slotID))
			return false;

		return ACM_PassesWeightCap(item);
	}

	float ACM_GetMaxWeight()
	{
		return m_fACM_MaxWeight;
	}

	// Sums the current contents' weights and rejects when adding `incoming`
	// would exceed `m_fACM_MaxWeight`. A non-positive cap disables enforcement.
	protected bool ACM_PassesWeightCap(IEntity incoming)
	{
		if (m_fACM_MaxWeight <= 0)
			return true;

		float incomingWeight = ACM_GetEntityWeight(incoming);
		float currentWeight = ACM_SumStoredWeight();

		if (currentWeight + incomingWeight > m_fACM_MaxWeight)
			return false;

		return true;
	}

	protected float ACM_SumStoredWeight()
	{
		float total = 0;
		int slotCount = GetSlotsCount();
		for (int i = 0; i < slotCount; i++)
		{
			InventoryStorageSlot slot = GetSlot(i);
			if (!slot)
				continue;

			IEntity stored = slot.GetAttachedEntity();
			if (!stored)
				continue;

			total += ACM_GetEntityWeight(stored);
		}
		return total;
	}

	protected float ACM_GetEntityWeight(IEntity entity)
	{
		if (!entity)
			return 0;

		// Vanilla API: returns the item's total weight including its own
		// contents, so a partially-filled pouch counts correctly toward the cap.
		InventoryItemComponent invItem = InventoryItemComponent.Cast(entity.FindComponent(InventoryItemComponent));
		if (invItem)
			return invItem.GetTotalWeight();

		return 0;
	}
}

