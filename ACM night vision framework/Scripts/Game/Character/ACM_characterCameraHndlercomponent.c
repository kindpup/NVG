modded class SCR_CharacterCameraHandlerComponent
{
	//------------------------------------------------------------------------------------------------
	override void OnAlphatestChange(int a)
	{
		super.OnAlphatestChange(a);
		if (m_LoadoutStorage)
		{
			auto ent = m_LoadoutStorage.GetClothFromArea(LoadoutHeadCoverArea);
			if (ent)
			{
				auto cloth = BaseLoadoutClothComponent.Cast(ent.FindComponent(BaseLoadoutClothComponent));
				if (cloth)
					cloth.SetAlpha(a);
			}

			auto Waist = m_LoadoutStorage.GetClothFromArea(ACM_Waist);
			if (Waist)
			{
				auto cloth = BaseLoadoutClothComponent.Cast(Waist.FindComponent(BaseLoadoutClothComponent));
				if (cloth)
					cloth.SetAlpha(a);
			}

			auto DropLegLeft = m_LoadoutStorage.GetClothFromArea(ACM_L_Leg);
			if (DropLegLeft)
			{
				auto cloth = BaseLoadoutClothComponent.Cast(DropLegLeft.FindComponent(BaseLoadoutClothComponent));
				if (cloth)
					cloth.SetAlpha(a);
			}

			auto DropLegRight = m_LoadoutStorage.GetClothFromArea(ACM_R_Leg);
			if (DropLegRight)
			{
				auto cloth = BaseLoadoutClothComponent.Cast(DropLegRight.FindComponent(BaseLoadoutClothComponent));
				if (cloth)
					cloth.SetAlpha(a);
			}

			auto Sling = m_LoadoutStorage.GetClothFromArea(ACM_Sling);
			if (Sling)
			{
				auto cloth = BaseLoadoutClothComponent.Cast(Sling.FindComponent(BaseLoadoutClothComponent));
				if (cloth)
					cloth.SetAlpha(a);
			}

			auto Mask = m_LoadoutStorage.GetClothFromArea(ACM_Mask);
			if (Mask)
			{
				auto cloth = BaseLoadoutClothComponent.Cast(Mask.FindComponent(BaseLoadoutClothComponent));
				if (cloth)
					cloth.SetAlpha(a);
			}

			auto Eyewear = m_LoadoutStorage.GetClothFromArea(ACM_Eyewear);
			if (Eyewear)
			{
				auto cloth = BaseLoadoutClothComponent.Cast(Eyewear.FindComponent(BaseLoadoutClothComponent));
				if (cloth)
					cloth.SetAlpha(a);
			}
		}

		// ACM-routed items attach as inventory slot children, not as worn cloth, so they skip
		// the vanilla cloth-area fade path. Mirror the same SetAlpha() treatment for any item
		// flagged by `ACM_FirstPersonHideTypes.ShouldHideInFirstPerson` (auto-detected via the
		// `ACM_AUTO_ROUTED_BEGIN`/`_END` sentinel range in the modded ECommonItemType enum).
		ACM_ApplyAlphaToRoutedHideItems(a);
	}

	protected void ACM_ApplyAlphaToRoutedHideItems(int a)
	{
		IEntity owner = GetOwner();
		if (!owner)
			return;

		SCR_InventoryStorageManagerComponent inv = SCR_InventoryStorageManagerComponent.Cast(owner.FindComponent(SCR_InventoryStorageManagerComponent));
		if (!inv)
			return;

		array<BaseInventoryStorageComponent> storages = {};
		inv.GetStorages(storages);

		foreach (BaseInventoryStorageComponent storage : storages)
		{
			if (!storage)
				continue;

			int slotCount = storage.GetSlotsCount();
			for (int i = 0; i < slotCount; i++)
			{
				InventoryStorageSlot slot = storage.GetSlot(i);
				if (!slot)
					continue;

				IEntity attached = slot.GetAttachedEntity();
				if (!attached)
					continue;

				if (!ACM_FirstPersonHideTypes.ShouldHideInFirstPerson(attached))
					continue;

				BaseLoadoutClothComponent cloth = BaseLoadoutClothComponent.Cast(attached.FindComponent(BaseLoadoutClothComponent));
				if (cloth)
					cloth.SetAlpha(a);
			}
		}
	}
};

