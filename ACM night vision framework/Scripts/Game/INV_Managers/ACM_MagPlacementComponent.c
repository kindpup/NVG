//
//  ACM
//  ACM Mag Placement Component
//
//  Defines per-mag-type slot placements for pouches/placards.
//==================================================================================================

[BaseContainerProps()]
class ACM_MagPlacementData
{
	[Attribute(defvalue: "", desc: "Exact magazine/item prefab resource path/name for this placement. Paste/copy the prefab resource here. Leave empty to use Mag Type fallback.")]
	string Prefab;

	[Attribute(defvalue: "NONE", uiwidget: UIWidgets.ComboBox, desc: "Fallback mag/common item type for this placement entry.", enums: ParamEnumArray.FromEnum(ECommonItemType))]
	string MagType;

	[Attribute(defvalue: "", desc: "Must match the ACM Slot ID on the ACM_EquipmentStorageSlot that should receive this mag/item.")]
	string PlacementID;

	[Attribute(defvalue: "0 0 0", desc: "Local visual offset applied to the attached mag after it is stored.")]
	vector Offset;

	[Attribute(defvalue: "0 0 0", desc: "Local visual rotation applied to the attached mag after it is stored. Enfusion angle vector format.")]
	vector Angles;
}

[ComponentEditorProps(category: "GameScripted/Inventory", description: "ACM Mag Placement Component")]
class ACM_MagPlacementComponentClass : ScriptComponentClass {}

[ComponentEditorProps(category: "GameScripted/Inventory", description: "ACM Mag Placement Component")]
class ACM_MagPlacementComponent : ScriptComponent
{
	[Attribute(defvalue: "", uiwidget: UIWidgets.Object, desc: "Per-mag-type placement rules. Add one entry per mag category, then point PlacementID at the matching ACM Slot ID on the storage slot.")]
	protected ref array<ref ACM_MagPlacementData> m_aPlacements;

	// Returns the exact prefab/resource name for an attached item.
	protected string ACM_GetEntityPrefabName(IEntity item)
	{
		if (!item)
			return string.Empty;

		EntityPrefabData prefabData = item.GetPrefabData();
		if (!prefabData)
			return string.Empty;

		return prefabData.GetPrefabName();
	}

	// Best lookup for routing: exact Prefab match first, then Mag Type fallback.
	ACM_MagPlacementData GetPlacementForItem(IEntity item)
	{
		if (!m_aPlacements || !item)
			return null;

		string itemPrefab = ACM_GetEntityPrefabName(item);
		if (!itemPrefab.IsEmpty())
		{
			foreach (ACM_MagPlacementData entry : m_aPlacements)
			{
				if (!entry || entry.Prefab.IsEmpty())
					continue;

				if (entry.Prefab == itemPrefab)
					return entry;
			}
		}

		ECommonItemType itemType = ACM_ItemClassificationService.GetNormalizedCommonType(item);
		return GetPlacementForType(itemType);
	}

	// Best lookup after attachment: exact Prefab + matching Slot ID first, then Slot ID fallback.
	ACM_MagPlacementData GetPlacementForItemAndSlot(IEntity item, string slotID)
	{
		if (!m_aPlacements || !item || slotID.IsEmpty())
			return null;

		string itemPrefab = ACM_GetEntityPrefabName(item);
		if (!itemPrefab.IsEmpty())
		{
			foreach (ACM_MagPlacementData entry : m_aPlacements)
			{
				if (!entry || entry.Prefab.IsEmpty() || entry.PlacementID.IsEmpty())
					continue;

				if (entry.Prefab == itemPrefab && entry.PlacementID == slotID)
					return entry;
			}
		}

		return GetPlacementForSlotID(slotID);
	}

	// Returns the placement entry for the given normalized mag/common item type, or null if not found.
	ACM_MagPlacementData GetPlacementForType(ECommonItemType magType)
	{
		if (!m_aPlacements)
			return null;

		foreach (ACM_MagPlacementData entry : m_aPlacements)
		{
			if (!entry || entry.MagType.IsEmpty() || entry.MagType == "NONE")
				continue;

			int parsedType = typename.StringToEnum(ECommonItemType, entry.MagType);
			if (parsedType < 0)
				continue;

			if (ACM_ItemClassificationService.NormalizeCommonItemType(parsedType) == magType)
				return entry;
		}

		return null;
	}

	// Returns the placement entry matching a storage slot ID.
	// This is used after attachment so Offset/Angles can be applied visually.
	ACM_MagPlacementData GetPlacementForSlotID(string slotID)
	{
		if (!m_aPlacements || slotID.IsEmpty())
			return null;

		foreach (ACM_MagPlacementData entry : m_aPlacements)
		{
			if (!entry || entry.PlacementID.IsEmpty())
				continue;

			if (entry.PlacementID == slotID)
				return entry;
		}

		return null;
	}
}

