modded class SCR_InventoryStoragesListUI
{
	override protected SCR_InventorySlotUI CreateStorageSlotUI(notnull InventoryItemComponent pItemComponent)
	{
		if (pItemComponent.GetParentSlot().Type() == SCR_TourniquetStorageSlot || pItemComponent.GetParentSlot().Type() == SCR_SalineBagStorageSlot)
			return null;

		// Use IsInherited so ACM_ClothNodeStorageComponent (and any future subclass)
		// gets the LBS-style slot UI instead of falling through to the generic
		// backpack UI, which doesn't fit the cloth-node layout slot.
		if (pItemComponent.Type().IsInherited(ClothNodeStorageComponent))
			return new SCR_InventorySlotLBSUI(pItemComponent, this);

		if (BaseInventoryStorageComponent.Cast(pItemComponent))
			return new SCR_InventorySlotStorageUI(pItemComponent, this);

		return new SCR_InventorySlotUI(pItemComponent, this);
	}
}
