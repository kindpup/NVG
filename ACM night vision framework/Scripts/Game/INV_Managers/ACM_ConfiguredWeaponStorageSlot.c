//
//
//  ACM
//  ACM Configured Weapon Storage Slot
//
//  Original ACM-owned script for the modular framework.
//==================================================================================================

class ACM_ConfiguredWeaponStorageSlot : ACM_PolicyDrivenStorageSlot
{
	[Attribute(EWeaponType.WT_NONE.ToString(), UIWidgets.ComboBox,
		desc: "Compatible weapon types for this slot. Uses legacy Allowed Weapon Type fallback when empty.",
		category: "ACM", enums: ParamEnumArray.FromEnum(EWeaponType))]
	protected ref array<string> m_aAllowedWeaponTypes;

	// Legacy single-value field kept for backward compatibility with existing serialized prefabs.
	protected string m_sAllowedWeaponType;

	protected EWeaponType m_eAllowedWeaponType;
	protected ref array<EWeaponType> m_ACM_AllowedWeaponTypes = {};

	protected EWeaponType ACM_GetAllowedWeaponType()
	{
		ACM_BuildAllowedWeaponTypeCache();
		if (m_ACM_AllowedWeaponTypes && !m_ACM_AllowedWeaponTypes.IsEmpty())
			return m_ACM_AllowedWeaponTypes[0];

		if (m_eAllowedWeaponType != 0)
			return m_eAllowedWeaponType;

		if (!m_sAllowedWeaponType.IsEmpty())
			m_eAllowedWeaponType = typename.StringToEnum(EWeaponType, m_sAllowedWeaponType);
		else
			m_eAllowedWeaponType = ACM_GetDefaultWeaponType();

		return m_eAllowedWeaponType;
	}

	protected void ACM_GetAllowedWeaponTypes(out array<EWeaponType> allowedWeaponTypes)
	{
		allowedWeaponTypes.Clear();
		ACM_BuildAllowedWeaponTypeCache();

		foreach (EWeaponType allowedWeaponType : m_ACM_AllowedWeaponTypes)
			allowedWeaponTypes.Insert(allowedWeaponType);
	}

	protected bool m_bACM_AllowedWeaponTypeCacheBuilt;

	protected void ACM_BuildAllowedWeaponTypeCache()
	{
		// Attributes never change at runtime, so parse once. This method is invoked
		// per-item per-slot during routing scans; re-parsing StringToEnum on every
		// call made it a cache in name only.
		if (m_bACM_AllowedWeaponTypeCacheBuilt)
			return;

		m_bACM_AllowedWeaponTypeCacheBuilt = true;

		if (!m_ACM_AllowedWeaponTypes)
			m_ACM_AllowedWeaponTypes = {};

		m_ACM_AllowedWeaponTypes.Clear();

		if (m_aAllowedWeaponTypes)
		{
			foreach (string weaponTypeName : m_aAllowedWeaponTypes)
			{
				if (weaponTypeName.IsEmpty())
					continue;

				int parsedWeaponType = typename.StringToEnum(EWeaponType, weaponTypeName);
				if (parsedWeaponType < 0)
					continue;

				EWeaponType weaponType = parsedWeaponType;
				if (weaponType == EWeaponType.WT_NONE)
					continue;

				if (m_ACM_AllowedWeaponTypes.Find(weaponType) == -1)
					m_ACM_AllowedWeaponTypes.Insert(weaponType);
			}
		}

		if (m_ACM_AllowedWeaponTypes.IsEmpty())
		{
			if (m_eAllowedWeaponType != 0)
			{
				if (m_eAllowedWeaponType != EWeaponType.WT_NONE)
					m_ACM_AllowedWeaponTypes.Insert(m_eAllowedWeaponType);
			}
			else if (!m_sAllowedWeaponType.IsEmpty())
			{
				int parsedLegacyWeaponType = typename.StringToEnum(EWeaponType, m_sAllowedWeaponType);
				if (parsedLegacyWeaponType >= 0)
				{
					m_eAllowedWeaponType = parsedLegacyWeaponType;
					if (m_eAllowedWeaponType != EWeaponType.WT_NONE)
						m_ACM_AllowedWeaponTypes.Insert(m_eAllowedWeaponType);
				}
			}
		}

		if (m_ACM_AllowedWeaponTypes.IsEmpty())
			m_ACM_AllowedWeaponTypes.Insert(ACM_GetDefaultWeaponType());
	}

	protected EWeaponType ACM_GetDefaultWeaponType()
	{
		return EWeaponType.WT_NONE;
	}
}
