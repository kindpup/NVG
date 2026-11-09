//------------------------------------------------------------------------------------------------
// ACM_RadialMenuCondition.c
// Condition for showing radial menu when ACM attachments are present
// Checks weapon attachments AND loadout equipment (helmet, NVG slot, strobe, lights)
// Part of ACM Night Vision and Lights mod
//------------------------------------------------------------------------------------------------

[BaseContainerProps()]
class ACM_RadialMenuCondition : SCR_AvailableActionCondition
{
	override bool IsAvailable(notnull SCR_AvailableActionsConditionData data)
	{
		if (!data)
		{
			return false;
		}
		
		ChimeraCharacter character = ChimeraCharacter.Cast(SCR_PlayerController.GetLocalControlledEntity());
		if (!character)
		{
			return false;
		}
		
		
		// Check weapon attachments
		BaseWeaponComponent weaponComponent = data.GetCurrentWeapon();
		if (weaponComponent)
		{
			array<AttachmentSlotComponent> attachments();
			weaponComponent.GetAttachments(attachments);
			
			
			foreach (AttachmentSlotComponent attachment : attachments)
			{
				IEntity attachedEntity = attachment.GetAttachedEntity();
				if (!attachedEntity)
					continue;
				
				
				if (ACM_NVL_LightsComponent.Cast(attachedEntity.FindComponent(ACM_NVL_LightsComponent)))
				{
					return true;
				}
				
				if (ACM_NVL_StrobeComponent.Cast(attachedEntity.FindComponent(ACM_NVL_StrobeComponent)))
				{
					return true;
				}
			}
		}
		else
		{
		}
		
		// Check character loadout equipment
		bool hasEquipment = HasACMEquipment(character);
		
		if (hasEquipment)
			return true;
		
		return false;
	}
	
	//------------------------------------------------------------------------------------------------
	// Check if character has any ACM equipment in loadout slots
	//------------------------------------------------------------------------------------------------
	protected bool HasACMEquipment(ChimeraCharacter character)
	{
		if (!character)
			return false;
		
		// Get equipped loadout storage component
		EquipedLoadoutStorageComponent loadout = EquipedLoadoutStorageComponent.Cast(
			character.FindComponent(EquipedLoadoutStorageComponent));
		
		if (!loadout)
		{
			return false;
		}
		
		
		// Check helmet area and its children (uses base game type)
		IEntity helmet = loadout.GetClothFromArea(LoadoutHeadCoverArea);
		if (helmet)
		{
			if (CheckEntityAndChildren(helmet))
			{
				return true;
			}
		}
		else
		{
		}
		
		// Scan all equipped items for ACM components
		array<IEntity> allEquipped = {};
		loadout.GetAll(allEquipped);
		
		foreach (IEntity item : allEquipped)
		{
			if (item)
			{
				if (CheckEntityAndChildren(item))
				{
					return true;
				}
			}
		}
		
		return false;
	}
	
	//------------------------------------------------------------------------------------------------
	// Check entity and all its children for ACM components
	//------------------------------------------------------------------------------------------------
	protected bool CheckEntityAndChildren(IEntity entity)
	{
		if (!entity)
			return false;
		
		// Check the entity itself
		if (HasACMComponent(entity))
		{
			return true;
		}
		
		// Check children
		IEntity child = entity.GetChildren();
		while (child)
		{
			if (CheckEntityAndChildren(child))
				return true;
			child = child.GetSibling();
		}
		
		// Check inventory storage for attached items
		BaseInventoryStorageComponent storage = BaseInventoryStorageComponent.Cast(
			entity.FindComponent(BaseInventoryStorageComponent));
		
		if (storage)
		{
			array<IEntity> items = {};
			storage.GetAll(items);
			foreach (IEntity item : items)
			{
				if (item && CheckEntityAndChildren(item))
					return true;
			}
		}
		
		return false;
	}
	
	//------------------------------------------------------------------------------------------------
	protected bool HasACMComponent(IEntity entity)
	{
		if (!entity)
			return false;
		
		ACM_NVL_LightsComponent lightsComp = ACM_NVL_LightsComponent.Cast(entity.FindComponent(ACM_NVL_LightsComponent));
		if (lightsComp)
		{
			return true;
		}
		
		ACM_NVL_StrobeComponent strobeComp = ACM_NVL_StrobeComponent.Cast(entity.FindComponent(ACM_NVL_StrobeComponent));
		if (strobeComp)
		{
			return true;
		}
		
		return false;
	}
}

//------------------------------------------------------------------------------------------------
// Condition for checking specific light type attachment
//------------------------------------------------------------------------------------------------
[BaseContainerProps()]
class ACM_LightAttachmentCondition : SCR_AvailableActionCondition
{
	[Attribute("0", UIWidgets.ComboBox, enums: ParamEnumArray.FromEnum(ACM_NVL_LightType), desc: "Light type to check for")]
	protected ACM_NVL_LightType m_iLightType;
	
	override bool IsAvailable(notnull SCR_AvailableActionsConditionData data)
	{
		if (!data)
			return false;
		
		ChimeraCharacter character = ChimeraCharacter.Cast(SCR_PlayerController.GetLocalControlledEntity());
		if (!character)
			return false;
		
		// Check weapon attachments
		BaseWeaponComponent weaponComponent = data.GetCurrentWeapon();
		if (weaponComponent)
		{
			array<AttachmentSlotComponent> attachments();
			weaponComponent.GetAttachments(attachments);
			
			foreach (AttachmentSlotComponent attachment : attachments)
			{
				IEntity attachedEntity = attachment.GetAttachedEntity();
				if (!attachedEntity)
					continue;
				
				ACM_NVL_LightsComponent lightsComp = ACM_NVL_LightsComponent.Cast(
					attachedEntity.FindComponent(ACM_NVL_LightsComponent));
				
				if (lightsComp && lightsComp.HasLightType(m_iLightType))
					return true;
			}
		}
		
		// Check loadout equipment
		if (HasEquipmentLightType(character, m_iLightType))
			return true;
		
		return false;
	}
	
	//------------------------------------------------------------------------------------------------
	protected bool HasEquipmentLightType(ChimeraCharacter character, ACM_NVL_LightType lightType)
	{
		if (!character)
			return false;
		
		EquipedLoadoutStorageComponent loadout = EquipedLoadoutStorageComponent.Cast(
			character.FindComponent(EquipedLoadoutStorageComponent));
		
		if (!loadout)
			return false;
		
		// Check helmet (uses base game type)
		IEntity helmet = loadout.GetClothFromArea(LoadoutHeadCoverArea);
		if (helmet && CheckEntityForLightType(helmet, lightType))
			return true;
		
		// Scan all equipped items
		array<IEntity> allEquipped = {};
		loadout.GetAll(allEquipped);
		foreach (IEntity item : allEquipped)
		{
			if (item && CheckEntityForLightType(item, lightType))
				return true;
		}
		
		return false;
	}
	
	//------------------------------------------------------------------------------------------------
	protected bool CheckEntityForLightType(IEntity entity, ACM_NVL_LightType lightType)
	{
		if (!entity)
			return false;
		
		// Check entity itself
		ACM_NVL_LightsComponent lightsComp = ACM_NVL_LightsComponent.Cast(
			entity.FindComponent(ACM_NVL_LightsComponent));
		if (lightsComp && lightsComp.HasLightType(lightType))
			return true;
		
		// Check children
		IEntity child = entity.GetChildren();
		while (child)
		{
			if (CheckEntityForLightType(child, lightType))
				return true;
			child = child.GetSibling();
		}
		
		// Check storage
		BaseInventoryStorageComponent storage = BaseInventoryStorageComponent.Cast(
			entity.FindComponent(BaseInventoryStorageComponent));
		
		if (storage)
		{
			array<IEntity> items = {};
			storage.GetAll(items);
			foreach (IEntity item : items)
			{
				if (item && CheckEntityForLightType(item, lightType))
					return true;
			}
		}
		
		return false;
	}
}
