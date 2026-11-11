//
//  ACM
//  ACM Patch Storage Slot
//
//  Original ACM-owned script for the modular framework.
//==================================================================================================

class ACM_PatchStorageSlot : ACM_PolicyDrivenStorageSlot
{
	override protected bool ACM_MatchesStoredItem(IEntity item)
	{
		return ACM_StoragePolicy.MatchesPatchItem(item);
	}

	override protected bool ACM_MatchesStoredResource(ResourceName resourceName)
	{
		return ACM_StoragePolicy.MatchesPatchResource(resourceName);
	}
}
