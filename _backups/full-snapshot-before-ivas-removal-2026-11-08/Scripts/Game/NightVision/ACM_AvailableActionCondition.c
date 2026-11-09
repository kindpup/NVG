// ACM NVG Action Condition
// Determines availability of NVG-related UI hints and actions

[BaseContainerProps()]
class ACM_NightVisionCondition : SCR_AvailableActionCondition
{
	[Attribute("0", UIWidgets.CheckBox, desc: "Verify brightness adjustment is available")]
	protected bool m_bRequireBrightnessControl;
	
	[Attribute("0", UIWidgets.CheckBox, desc: "Verify brightness differs from default")]
	protected bool m_bRequireBrightnessChanged;
	
	//------------------------------------------------------------------------------------------------
	override bool IsAvailable(notnull SCR_AvailableActionsConditionData data)
	{
		if (!data)
			return GetReturnResult(false);
		
		SCR_ChimeraCharacter player = SCR_ChimeraCharacter.Cast(data.GetCharacter());
		if (!player)
			return GetReturnResult(false);
		
		EquipedLoadoutStorageComponent loadout = EquipedLoadoutStorageComponent.Cast(
			player.FindComponent(EquipedLoadoutStorageComponent));
		
		if (!loadout)
			return GetReturnResult(false);
		
		// Try dedicated NVG slot first
		IEntity equipment = loadout.GetClothFromArea(ACM_NV_ItemSlot);
		
		// Fall back to headgear slot
		if (!equipment)
		{
			equipment = loadout.GetClothFromArea(LoadoutHeadCoverArea);
			if (!equipment)
				return GetReturnResult(false);
		}
		
		// Find NVG component
		ACM_NightVisionComponent nvgComp = ACM_NightVisionComponent.Cast(
			equipment.FindComponent(ACM_NightVisionComponent));
		
		// If not directly on equipment, search attached items
		if (!nvgComp)
		{
			ClothNodeStorageComponent storage = ClothNodeStorageComponent.Cast(
				equipment.FindComponent(ClothNodeStorageComponent));
			
			if (!storage)
				return GetReturnResult(false);
			
			array<IEntity> attachedItems = {};
			storage.GetAll(attachedItems);
			
			foreach (IEntity item : attachedItems)
			{
				nvgComp = ACM_NightVisionComponent.Cast(item.FindComponent(ACM_NightVisionComponent));
				if (nvgComp)
					break;
			}
			
			if (!nvgComp)
				return GetReturnResult(false);
		}
		
		// Basic presence check
		if (!m_bRequireBrightnessControl && !m_bRequireBrightnessChanged)
			return GetReturnResult(true);
		
		// Brightness control check
		if (m_bRequireBrightnessControl && !m_bRequireBrightnessChanged)
		{
			if (nvgComp.IsToggledOn())
				return GetReturnResult(nvgComp.IsManualAdjustEnabled());
		}
		
		// Brightness changed check (for reset hint)
		if (m_bRequireBrightnessChanged)
		{
			if (nvgComp.IsToggledOn() && nvgComp.IsManualAdjustEnabled())
			{
				bool brightnessModified = (nvgComp.GetBrightness() != nvgComp.GetBaseBrightness());
				return GetReturnResult(brightnessModified);
			}
		}
		
		return GetReturnResult(false);
	}
}
