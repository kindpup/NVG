//
//  ACM
//  ACM Storage Route Candidate
//
//  Original ACM-owned script for the modular framework.
//==================================================================================================

enum ACM_EStorageRouteResult
{
	FAILED,
	FOUND,
	OCCUPIED,
	INCOMPATIBLE
}

class ACM_StorageRouteCandidate
{
	BaseInventoryStorageComponent Storage;
	InventoryStorageSlot Slot;
	int SlotIndex = -1;
	int Score = 0;
	bool IsReplacement = false;
	ACM_EStorageRouteResult Result = ACM_EStorageRouteResult.FAILED;

	void ACM_StorageRouteCandidate(
		BaseInventoryStorageComponent targetStorage = null,
		InventoryStorageSlot targetSlot = null,
		int targetSlotIndex = -1,
		int targetScore = 0,
		bool targetIsReplacement = false)
	{
		Storage = targetStorage;
		Slot = targetSlot;
		SlotIndex = targetSlotIndex;
		Score = targetScore;
		IsReplacement = targetIsReplacement;

		if (Storage && Slot && SlotIndex >= 0)
			Result = ACM_EStorageRouteResult.FOUND;
	}

	bool IsValid()
	{
		return Result == ACM_EStorageRouteResult.FOUND && Storage != null && Slot != null && SlotIndex >= 0;
	}
}
