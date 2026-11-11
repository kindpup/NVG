modded class SCR_InventoryMenuUI
{
	override void ShowStorage(BaseInventoryStorageComponent storage, LoadoutAreaType area = null)
	{
		if (!m_wStoragesContainer)
			return;

		m_wContainer = GetGame().GetWorkspace().CreateWidgets(BACKPACK_STORAGE_LAYOUT, m_wStoragesContainer);

		int row = Math.Floor(m_iStorageListCounter / STORAGE_AREA_COLUMNS);
		int column = m_iStorageListCounter % STORAGE_AREA_COLUMNS;
		m_iStorageListCounter++;

		m_wStoragesContainer.SetRowFillWeight(row, 0);
		m_wStoragesContainer.SetColumnFillWeight(column, 0);

		GridSlot.SetColumn(m_wContainer, column);
		GridSlot.SetRow(m_wContainer, row);
		GridSlot.SetColumnSpan(m_wContainer, 1);
		GridSlot.SetRowSpan(m_wContainer, 1);

		if (!m_wContainer)
			return;

		// Use IsInherited so ACM_ClothNodeStorageComponent (and any future subclass)
		// gets the LBS storage panel instead of the backpack panel.
		if (storage.Type().IsInherited(ClothNodeStorageComponent))
		{
			m_wContainer.AddHandler(new SCR_InventoryStorageLBSUI(storage, area, this, 0, null));
			m_pStorageBaseUI = SCR_InventoryStorageBaseUI.Cast(m_wContainer.FindHandler(SCR_InventoryStorageLBSUI));
		}
		else if (storage.Type() == EquipedWeaponStorageComponent)
		{
			m_wContainer.AddHandler(new SCR_InventoryStorageWeaponsUI(m_StorageManager.GetWeaponStorage(), area, this));
			m_pStorageBaseUI = SCR_InventoryStorageWeaponsUI.Cast(m_wContainer.FindHandler(SCR_InventoryStorageWeaponsUI));
		}
		else
		{
			m_wContainer.AddHandler(new SCR_InventoryStorageBackpackUI(storage, area, this, 0, null));
			m_pStorageBaseUI = SCR_InventoryStorageBaseUI.Cast(m_wContainer.FindHandler(SCR_InventoryStorageBaseUI));
		}

		m_aStorages.Insert(m_pStorageBaseUI);
	}
}
