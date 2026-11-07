// ACM Inventory UI Extensions
// Adds support for displaying NVG equipment in inventory screens

//------------------------------------------------------------------------------------------------
// Player loadout inventory panel
modded class SCR_InventoryStorageLBSUI
{
	//------------------------------------------------------------------------------------------------
	protected override void GetAllItems(out notnull array<IEntity> pItemsInStorage, BaseInventoryStorageComponent pStorage = null)
	{
		super.GetAllItems(pItemsInStorage, pStorage);
		
		// Filter for NVG-related slot types only
		if (!GetSlotAreaType())
			return;
		
		typename slotType = GetSlotAreaType().Type();
		if (slotType != ACM_NV_ItemSlot && slotType != LoadoutHeadCoverArea)
			return;

		BaseInventoryStorageComponent storage = pStorage;
		if (!storage)
			storage = m_Storage;
		
		if (!storage)
			return;

		// Scan storage slots for slotted NVG items
		int slotCount = storage.GetSlotsCount();
		for (int i = 0; i < slotCount; i++)
		{
			InventoryStorageSlot slot = storage.GetSlot(i);
			if (!slot)
				continue;
			
			IEntity attachedItem = slot.GetAttachedEntity();
			if (!attachedItem || pItemsInStorage.Contains(attachedItem))
				continue;
			
			BaseLoadoutClothComponent cloth = BaseLoadoutClothComponent.Cast(
				attachedItem.FindComponent(BaseLoadoutClothComponent));
			
			if (!cloth || !cloth.GetAreaType())
				continue;
			
			if (cloth.GetAreaType().Type() == ACM_NV_Slotted)
				pItemsInStorage.Insert(attachedItem);
		}
	}
}
