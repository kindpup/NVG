//
//  ACM
//  ACM Item Attribute Collection Extension
//
//  Extends the stock item attribute collection with additional compatible item-type names for
//  ACM routing and slot matching.
//==================================================================================================

modded class SCR_ItemAttributeCollection
{
	[Attribute(ECommonItemType.NONE.ToString(), UIWidgets.ComboBox,
		"Additional ACM routing types. ACM uses these before the primary CommonItemType, which stays available for the base framework.", enums: ParamEnumArray.FromEnum(ECommonItemType))]
	protected ref array<string> m_ACM_AdditionalCommonItemTypes;

	array<string> ACM_GetAdditionalCommonItemTypeNames()
	{
		return m_ACM_AdditionalCommonItemTypes;
	}
}
