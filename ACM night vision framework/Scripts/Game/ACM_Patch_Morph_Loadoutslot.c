class ACM_GearMorphSlotInfo : LoadoutSlotInfo
{
	[Attribute("0", UIWidgets.CheckBox, "Enable Morphing", "If enabled, ACM morph targets will be applied to the attached entity.")]
	protected bool m_bEnableMorphing;

	[Attribute("0", UIWidgets.Slider, "Y Curve Amount", "-1 1 -1.0")]
	protected float m_fMorphY;

	[Attribute("0", UIWidgets.Slider, "X Curve Amount", "-1 1 -1.0")]
	protected float m_fMorphX;

	[Attribute("1", UIWidgets.CheckBox, "Morph Attached Chain", "If enabled, apply the same morph curves to attached child entities (Vest -> Backpanel -> Attachment).")]
	protected bool m_bMorphAttachedChain;

	protected void ACM_ApplyCurves(IEntity entity, float yAmount, float xAmount)
	{
		if (!entity)
			return;

		Animation anim = entity.GetAnimation();
		if (!anim)
			return;

		anim.SetMorphState("Y_Curve", yAmount);
		anim.SetMorphState("X_Curve", xAmount);
	}

	protected void ACM_AddStorageIfMissing(notnull array<BaseInventoryStorageComponent> storages, BaseInventoryStorageComponent storage)
	{
		if (!storage)
			return;

		foreach (BaseInventoryStorageComponent existing : storages)
		{
			if (existing == storage)
				return;
		}

		storages.Insert(storage);
	}

	protected void ACM_CollectEntityStorages(IEntity entity, out notnull array<BaseInventoryStorageComponent> storages)
	{
		storages.Clear();
		if (!entity)
			return;

		SCR_InventoryStorageManagerComponent storageManager = SCR_InventoryStorageManagerComponent.Cast(entity.FindComponent(SCR_InventoryStorageManagerComponent));
		if (storageManager)
			storageManager.GetStorages(storages);

		// Some attached loadout entities (for example backpanels) expose direct
		// storage components but no inventory manager component.
		ACM_AddStorageIfMissing(storages, ACM_EquipmentStorageComponent.Cast(entity.FindComponent(ACM_EquipmentStorageComponent)));
		ACM_AddStorageIfMissing(storages, ACM_PouchStorageComponent.Cast(entity.FindComponent(ACM_PouchStorageComponent)));
		ACM_AddStorageIfMissing(storages, ACM_UniversalStorageComponent.Cast(entity.FindComponent(ACM_UniversalStorageComponent)));
		ACM_AddStorageIfMissing(storages, SCR_UniversalInventoryStorageComponent.Cast(entity.FindComponent(SCR_UniversalInventoryStorageComponent)));
		ACM_AddStorageIfMissing(storages, ClothNodeStorageComponent.Cast(entity.FindComponent(ClothNodeStorageComponent)));
		ACM_AddStorageIfMissing(storages, ACM_ClothNodeStorageComponent.Cast(entity.FindComponent(ACM_ClothNodeStorageComponent)));
	}

	protected void ACM_CollectRootInventoryStorages(IEntity entity, out notnull array<BaseInventoryStorageComponent> storages)
	{
		storages.Clear();
		if (!entity)
			return;

		IEntity rootEntity = entity.GetRootParent();
		if (!rootEntity)
			rootEntity = entity;

		SCR_InventoryStorageManagerComponent rootStorageManager = SCR_InventoryStorageManagerComponent.Cast(rootEntity.FindComponent(SCR_InventoryStorageManagerComponent));
		if (!rootStorageManager)
			return;

		rootStorageManager.GetStorages(storages);
	}

	protected bool ACM_IsDescendantOf(IEntity candidate, IEntity potentialAncestor)
	{
		if (!candidate || !potentialAncestor || candidate == potentialAncestor)
			return false;

		IEntity parent = candidate.GetParent();
		while (parent)
		{
			if (parent == potentialAncestor)
				return true;

			parent = parent.GetParent();
		}

		return false;
	}

	// Applies the same morph values to entities attached under the given root.
	protected void ACM_ApplyCurvesToAttachedChain(IEntity rootEntity, float yAmount, float xAmount)
	{
		if (!rootEntity)
			return;

		array<BaseInventoryStorageComponent> storages = {};
		ACM_CollectEntityStorages(rootEntity, storages);
		if (storages.IsEmpty())
			return;

		foreach (BaseInventoryStorageComponent storage : storages)
		{
			if (!storage)
				continue;

			for (int slotIndex = 0; slotIndex < storage.GetSlotsCount(); slotIndex++)
			{
				InventoryStorageSlot slot = storage.GetSlot(slotIndex);
				if (!slot)
					continue;

				IEntity attachedEntity = slot.GetAttachedEntity();
				if (!attachedEntity || attachedEntity == rootEntity)
					continue;

				ACM_ApplyCurves(attachedEntity, yAmount, xAmount);
				ACM_ApplyCurvesToAttachedChain(attachedEntity, yAmount, xAmount);
			}
		}

		// Fallback for cloth/loadout attachment chains that are registered on the
		// player/root inventory graph but not discoverable directly on the entity.
		array<BaseInventoryStorageComponent> rootStorages = {};
		ACM_CollectRootInventoryStorages(rootEntity, rootStorages);

		foreach (BaseInventoryStorageComponent rootStorage : rootStorages)
		{
			if (!rootStorage)
				continue;

			for (int rootSlotIndex = 0; rootSlotIndex < rootStorage.GetSlotsCount(); rootSlotIndex++)
			{
				InventoryStorageSlot rootSlot = rootStorage.GetSlot(rootSlotIndex);
				if (!rootSlot)
					continue;

				IEntity rootAttachedEntity = rootSlot.GetAttachedEntity();
				if (!rootAttachedEntity || !ACM_IsDescendantOf(rootAttachedEntity, rootEntity))
					continue;

				ACM_ApplyCurves(rootAttachedEntity, yAmount, xAmount);
			}
		}
	}

	protected void ACM_ApplyMorphAndRefresh(IEntity entity, float yAmount, float xAmount)
	{
		if (!entity)
			return;

		ACM_ApplyCurves(entity, yAmount, xAmount);

		if (m_bMorphAttachedChain)
			ACM_ApplyCurvesToAttachedChain(entity, yAmount, xAmount);

		ACM_RefreshAttachedPrefabPlacement(entity);
	}

	// Replays slot-side attach transform logic for non-ACM slots whose placement
	// is managed by engine loadout code instead of ACM_EquipmentStorageSlot.
	protected void ACM_ReapplyGenericSlotAttachment(InventoryStorageSlot slot, IEntity attachedEntity)
	{
		if (!slot || !attachedEntity)
			return;

		if (ACM_GearMorphSlotInfo.Cast(slot))
			return;

		if (slot.GetAttachedEntity() != attachedEntity)
			return;

		slot.OnAttachedEntity(attachedEntity);
	}

	protected void ACM_RefreACMlotAttachment(InventoryStorageSlot slot, IEntity attachedEntity)
	{
		if (!slot || !attachedEntity)
			return;

		ACM_EquipmentStorageSlot ACMSlot = ACM_EquipmentStorageSlot.Cast(slot);
		if (ACMSlot)
			ACMSlot.ACM_RefreshAttachedVisualPlacement();
		else
		{
			ACM_ReapplyGenericSlotAttachment(slot, attachedEntity);
			GetGame().GetCallqueue().CallLater(ACM_ReapplyGenericSlotAttachment, 0, false, slot, attachedEntity);
			GetGame().GetCallqueue().CallLater(ACM_ReapplyGenericSlotAttachment, 100, false, slot, attachedEntity);
		}
	}

	// Reapplies attached-item placement offsets after a parent morph update so
	// mounted prefabs follow the morphed carrier location without morphing themselves.
	void ACM_RefreshAttachedPrefabPlacement(IEntity entity)
	{
		if (!entity)
			return;

		array<BaseInventoryStorageComponent> storages = {};
		ACM_CollectEntityStorages(entity, storages);
		if (storages.IsEmpty())
			return;

		foreach (BaseInventoryStorageComponent storage : storages)
		{
			if (!storage)
				continue;

			for (int slotIndex = 0; slotIndex < storage.GetSlotsCount(); slotIndex++)
			{
				InventoryStorageSlot slot = storage.GetSlot(slotIndex);
				if (!slot)
					continue;

				IEntity attachedEntity = slot.GetAttachedEntity();
				if (!attachedEntity || attachedEntity == entity)
					continue;

				ACM_RefreACMlotAttachment(slot, attachedEntity);

				ACM_RefreshAttachedPrefabPlacement(attachedEntity);
			}
		}

		array<BaseInventoryStorageComponent> rootStorages = {};
		ACM_CollectRootInventoryStorages(entity, rootStorages);

		foreach (BaseInventoryStorageComponent rootStorage : rootStorages)
		{
			if (!rootStorage)
				continue;

			for (int rootSlotIndex = 0; rootSlotIndex < rootStorage.GetSlotsCount(); rootSlotIndex++)
			{
				InventoryStorageSlot rootSlot = rootStorage.GetSlot(rootSlotIndex);
				if (!rootSlot)
					continue;

				IEntity rootAttachedEntity = rootSlot.GetAttachedEntity();
				if (!rootAttachedEntity || !ACM_IsDescendantOf(rootAttachedEntity, entity))
					continue;

				ACM_RefreACMlotAttachment(rootSlot, rootAttachedEntity);
			}
		}
	}

	protected void ACM_ResetCurves(IEntity entity)
	{
		ACM_ApplyCurves(entity, 0, 0);

		if (m_bMorphAttachedChain)
			ACM_ApplyCurvesToAttachedChain(entity, 0, 0);
	}

	override void OnAttachedEntity(IEntity entity)
	{
		super.OnAttachedEntity(entity);

		if (!m_bEnableMorphing)
			return;

		ACM_ApplyMorphAndRefresh(entity, m_fMorphY, m_fMorphX);

		// Some child attachments are created a frame later by loadout replay; refresh
		// placement again so late-attached prefabs still follow the morphed carrier.
		GetGame().GetCallqueue().CallLater(ACM_ApplyMorphAndRefresh, 0, false, entity, m_fMorphY, m_fMorphX);
		GetGame().GetCallqueue().CallLater(ACM_ApplyMorphAndRefresh, 100, false, entity, m_fMorphY, m_fMorphX);
	}

	override void OnDetachedEntity(IEntity entity)
	{
		if (m_bEnableMorphing)
			ACM_ResetCurves(entity);

		super.OnDetachedEntity(entity);
	}
}

