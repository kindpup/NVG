//
//  ACM
//  ACM Pouch Storage Component
//
//  Original ACM-owned script for the modular framework.
//==================================================================================================

class ACM_UniversalStorageComponentClass : SCR_UniversalInventoryStorageComponentClass {}

// Universal ACM storage still uses Reforger component hooks, but registration and item policy live in shared services.
class ACM_UniversalStorageComponent : SCR_UniversalInventoryStorageComponent
{
	// Registers this storage type for arsenal/loadout persistence because Reforger save code only checks registered component classes.
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		ACM_ArsenalLoadoutRegistration.EnsureRegistered();
	}

	// Leaves generic storage acceptance open so slot rules and caller logic can decide the actual fit.
	override event bool OnOverrideCanStoreItem()
	{
		return true;
	}

	// Leaves replacement checks open for the same reason as store checks on this generic storage wrapper.
	override event bool OnOverrideCanReplaceItem()
	{
		return true;
	}
}

class ACM_PouchStorageComponentClass : SCR_UniversalInventoryStorageComponentClass {}

// Pouches are a policy-driven storage surface: Reforger asks this component first, then ACM applies its own pouch rules.
class ACM_PouchStorageComponent : SCR_UniversalInventoryStorageComponent
{
	// Registers pouch storage for arsenal/loadout persistence so saved placards and pouch contents survive reload.
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

	// Honors the engine/base storage checks first, then applies ACM-specific pouch filtering.
	override bool CanStoreItem(IEntity item, int slotID)
	{
		return super.CanStoreItem(item, slotID) && ACM_AllowsPouchItem(item);
	}

	// Keeps handguns out of generic pouches so sidearms stay under holster and sidearm-routing rules.
	protected bool ACM_AllowsPouchItem(IEntity item)
	{
		if (!ACM_ItemClassificationService.IsHandgun(item))
			return true;

		ACM_SetRejectedAsOversized();
		return false;
	}

	// Returns a useful engine error code so the UI reports why the pouch rejected the item.
	protected void ACM_SetRejectedAsOversized()
	{
		if (pInventoryManager)
			pInventoryManager.SetReturnCode(EInventoryRetCode.RETCODE_ITEM_TOO_BIG);
	}
}
