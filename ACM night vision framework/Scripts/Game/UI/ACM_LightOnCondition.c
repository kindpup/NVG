//------------------------------------------------------------------------------------------------
// ACM_LightOnCondition.c
// Condition to check if any light (flashlight or IR) is currently on
// Used for showing light brightness control hints
//------------------------------------------------------------------------------------------------

[BaseContainerProps()]
class ACM_LightOnCondition : SCR_AvailableActionCondition
{
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
		
		// Check headgear slot
		IEntity headgear = loadout.GetClothFromArea(LoadoutHeadCoverArea);
		if (headgear && CheckEntityForActiveLight(headgear))
			return GetReturnResult(true);
		
		// Check vest/body slot
		IEntity vest = loadout.GetClothFromArea(LoadoutArmoredVestSlotArea);
		if (vest && CheckEntityForActiveLight(vest))
			return GetReturnResult(true);
		
		return GetReturnResult(false);
	}
	
	//------------------------------------------------------------------------------------------------
	protected bool CheckEntityForActiveLight(IEntity entity)
	{
		if (!entity)
			return false;
		
		// Check the entity itself
		ACM_NVL_LightsComponent lightsComp = ACM_NVL_LightsComponent.Cast(
			entity.FindComponent(ACM_NVL_LightsComponent));
		
		if (lightsComp && (lightsComp.IsFlashlightOn() || lightsComp.IsIROn()))
			return true;
		
		// Check attached items via ClothNodeStorageComponent
		ClothNodeStorageComponent storage = ClothNodeStorageComponent.Cast(
			entity.FindComponent(ClothNodeStorageComponent));
		
		if (storage)
		{
			array<IEntity> attachedItems = {};
			storage.GetAll(attachedItems);
			
			foreach (IEntity item : attachedItems)
			{
				if (!item)
					continue;
				
				ACM_NVL_LightsComponent itemLightsComp = ACM_NVL_LightsComponent.Cast(
					item.FindComponent(ACM_NVL_LightsComponent));
				
				if (itemLightsComp && (itemLightsComp.IsFlashlightOn() || itemLightsComp.IsIROn()))
					return true;
			}
		}
		
		// Check children entities
		IEntity child = entity.GetChildren();
		while (child)
		{
			ACM_NVL_LightsComponent childLightsComp = ACM_NVL_LightsComponent.Cast(
				child.FindComponent(ACM_NVL_LightsComponent));
			
			if (childLightsComp && (childLightsComp.IsFlashlightOn() || childLightsComp.IsIROn()))
				return true;
			
			child = child.GetSibling();
		}
		
		return false;
	}
}
