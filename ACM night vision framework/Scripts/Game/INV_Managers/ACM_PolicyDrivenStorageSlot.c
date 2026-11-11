//
//  ACM
//  ACM Policy Driven Storage Slot
//
//  Original ACM-owned script for the modular framework.
//==================================================================================================

class ACM_PolicyDrivenStorageSlot : SCR_EquipmentStorageSlot
{
	protected int m_iInteractionLayer;

	bool CanStoreItem(IEntity item)
	{
		return !GetAttachedEntity() && ACM_MatchesStoredItem(item);
	}

	override bool ACM_CanAcceptItem(IEntity item)
	{
		return CanStoreItem(item);
	}

	bool ACM_CanStoreResource(ResourceName resourceName)
	{
		return !GetAttachedEntity() && ACM_MatchesStoredResource(resourceName);
	}

	override bool ACM_CanAcceptResource(ResourceName resourceName)
	{
		return ACM_CanStoreResource(resourceName);
	}

	override bool ACM_CanAcceptExplicitReplayItem(IEntity item)
	{
		ACM_LoadoutReplayCompat.MarkReplayWindow();

		if (!item)
			return false;

		IEntity attached = GetAttachedEntity();
		if (attached && attached != item)
			return false;

		return ACM_MatchesStoredItem(item);
	}

	override bool ACM_CanAcceptExplicitReplayResource(ResourceName resourceName)
	{
		ACM_LoadoutReplayCompat.MarkReplayWindow();

		if (GetAttachedEntity())
			return false;

		return ACM_MatchesStoredResource(resourceName);
	}

	bool CanReplaceItem(IEntity nextItem)
	{
		return ACM_MatchesStoredItem(nextItem);
	}

	override bool ACM_CanReplaceWith(IEntity nextItem)
	{
		return CanReplaceItem(nextItem);
	}

	override void OnAttachedEntity(IEntity entity)
	{
		super.OnAttachedEntity(entity);
		// Diagnostic parity with ACM_EquipmentStorageSlot: policy slots (radio / nade /
		// patch / holster pouches) previously never logged, which made radio placement
		// issues impossible to trace. Logging only — no behavior change.
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

	protected bool ACM_MatchesStoredItem(IEntity item)
	{
		return false;
	}

	protected bool ACM_MatchesStoredResource(ResourceName resourceName)
	{
		return false;
	}
}

