//
//  ACM
//  ACM Radio Storage Slot
//
//  Original ACM-owned script for the modular framework.
//==================================================================================================

class ACM_RadioStorageSlot : ACM_PolicyDrivenStorageSlot
{
	override protected bool ACM_MatchesStoredItem(IEntity item)
	{
		return ACM_StoragePolicy.MatchesRadioItem(item);
	}

	override protected bool ACM_MatchesStoredResource(ResourceName resourceName)
	{
		return ACM_StoragePolicy.MatchesRadioResource(resourceName);
	}
}
