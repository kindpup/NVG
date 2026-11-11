//
//  ACM
//  ACM Item Type Override Component
//
//  Optional per-item override that lets ACM classify an item without depending on shared
//  CommonItemType values from external inventory frameworks.
//==================================================================================================

[BaseContainerProps()]
class ACM_ItemTypeOverrideData
{
	[Attribute(defvalue: "NONE", uiwidget: UIWidgets.ComboBox, desc: "Canonical ACM item type used by ACM routing/classification.", enums: ParamEnumArray.FromEnum(ECommonItemType))]
	string CommonType;

	[Attribute(defvalue: "NONE", uiwidget: UIWidgets.ComboBox, desc: "Optional secondary compatibility type. ACM will normalize either field to its canonical routing type.", enums: ParamEnumArray.FromEnum(ECommonItemType))]
	string SecondaryCommonType;
}

[ComponentEditorProps(category: "GameScripted/Inventory", description: "Optional ACM item type override used before shared CommonItemType values")]
class ACM_ItemTypeOverrideComponentClass : ScriptComponentClass {}

[ComponentEditorProps(category: "GameScripted/Inventory", description: "Optional ACM item type override used before shared CommonItemType values")]
class ACM_ItemTypeOverrideComponent : ScriptComponent
{
	[Attribute(defvalue: "", uiwidget: UIWidgets.Object, desc: "ACM-owned item type override. When set, ACM routing/classification uses this before CommonItemType.")]
	protected ref ACM_ItemTypeOverrideData m_ACM_TypeOverride;

	protected bool ACM_TryNormalizeTypeName(string rawTypeName, out ECommonItemType resolvedType)
	{
		resolvedType = ECommonItemType.NONE;

		if (rawTypeName.IsEmpty() || rawTypeName == ECommonItemType.NONE.ToString())
			return false;

		string normalizedTypeName = ACM_ItemClassificationService.NormalizeCommonItemTypeName(rawTypeName);
		int parsedType = typename.StringToEnum(ECommonItemType, normalizedTypeName);
		if (parsedType < 0)
			return false;

		resolvedType = parsedType;
		return resolvedType != ECommonItemType.NONE && resolvedType != -1;
	}

	bool ACM_TryGetNormalizedType(out ECommonItemType resolvedType)
	{
		resolvedType = ECommonItemType.NONE;

		if (!m_ACM_TypeOverride)
			return false;

		if (ACM_TryNormalizeTypeName(m_ACM_TypeOverride.CommonType, resolvedType))
			return true;

		return ACM_TryNormalizeTypeName(m_ACM_TypeOverride.SecondaryCommonType, resolvedType);
	}
}
