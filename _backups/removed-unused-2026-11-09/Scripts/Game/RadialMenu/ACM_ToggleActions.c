//------------------------------------------------------------------------------------------------
// ACM_ToggleActions.c
// User actions for toggling lights, lasers, and IR from radial menu
// Part of ACM Night Vision and Lights mod
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
// Toggle Flashlight Action
//------------------------------------------------------------------------------------------------
class ACM_ToggleFlashlightAction : ACM_FunctionAction
{
	protected ACM_NVL_LightsComponent m_LightsComponent;
	protected InventoryItemComponent m_InventoryItemComp;
	protected SCR_InventoryStorageManagerComponent m_InventoryManager;
	
	override void Init(IEntity pOwnerEntity, GenericComponent pManagerComponent)
	{
		m_LightsComponent = ACM_NVL_LightsComponent.Cast(pOwnerEntity.FindComponent(ACM_NVL_LightsComponent));
		m_InventoryItemComp = InventoryItemComponent.Cast(pOwnerEntity.FindComponent(InventoryItemComponent));
	}
	
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		ACM_NVL_LightsComponent lightComponent = ACM_NVL_LightsComponent.Cast(pOwnerEntity.FindComponent(ACM_NVL_LightsComponent));
		if (!lightComponent)
			return;
		
		lightComponent.RequestToggleFlashlight();
	}
	
	override bool CanBeShownScript(IEntity user)
	{
		if (!m_InventoryItemComp || !m_InventoryItemComp.GetParentSlot())
			return false;
		
		if (!m_LightsComponent)
			return false;
		
		ChimeraCharacter character = ChimeraCharacter.Cast(SCR_PlayerController.GetLocalControlledEntity());
		if (!character)
			return false;
		
		CharacterControllerComponent controller = character.GetCharacterController();	
		if (!controller)
			return false;		
		
		if (!controller.GetInspect())
			return false;
		
		BaseWeaponManagerComponent weaponManager = controller.GetWeaponManagerComponent();
		if (!weaponManager)
			return false;
		
		BaseWeaponComponent weaponComp = weaponManager.GetCurrentWeapon();
		if (!weaponComp || weaponComp.GetOwner() != m_InventoryItemComp.GetParentSlot().GetOwner())
			return false;
		
		return true;
	}
	
	override bool GetActionNameScript(out string outName)
	{
		if (!m_LightsComponent)
			return false;
		
		string actionName = GetUIInfo().GetName();
		string state = "OFF";
		if (m_LightsComponent.IsFlashlightOn())
			state = "ON";
		
		outName = string.Format("Toggle: %1 [%2]", actionName, state);
		return true;
	}
	
	override bool CanBroadcastScript() 
	{ 
		return false;
	}
}

//------------------------------------------------------------------------------------------------
// Toggle IR Action
//------------------------------------------------------------------------------------------------
class ACM_ToggleIRAction : ACM_FunctionAction
{
	protected ACM_NVL_LightsComponent m_LightsComponent;
	protected InventoryItemComponent m_InventoryItemComp;
	protected SCR_InventoryStorageManagerComponent m_InventoryManager;
	
	override void Init(IEntity pOwnerEntity, GenericComponent pManagerComponent)
	{
		m_LightsComponent = ACM_NVL_LightsComponent.Cast(pOwnerEntity.FindComponent(ACM_NVL_LightsComponent));
		m_InventoryItemComp = InventoryItemComponent.Cast(pOwnerEntity.FindComponent(InventoryItemComponent));
	}
	
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		ACM_NVL_LightsComponent lightComponent = ACM_NVL_LightsComponent.Cast(pOwnerEntity.FindComponent(ACM_NVL_LightsComponent));
		if (!lightComponent)
			return;
		
		lightComponent.RequestToggleIR();
	}
	
	override bool CanBeShownScript(IEntity user)
	{
		if (!m_InventoryItemComp || !m_InventoryItemComp.GetParentSlot())
			return false;
		
		if (!m_LightsComponent)
			return false;
		
		ChimeraCharacter character = ChimeraCharacter.Cast(SCR_PlayerController.GetLocalControlledEntity());
		if (!character)
			return false;
		
		CharacterControllerComponent controller = character.GetCharacterController();	
		if (!controller)
			return false;		
		
		if (!controller.GetInspect())
			return false;
		
		BaseWeaponManagerComponent weaponManager = controller.GetWeaponManagerComponent();
		if (!weaponManager)
			return false;
		
		BaseWeaponComponent weaponComp = weaponManager.GetCurrentWeapon();
		if (!weaponComp || weaponComp.GetOwner() != m_InventoryItemComp.GetParentSlot().GetOwner())
			return false;
		
		return true;
	}
	
	override bool GetActionNameScript(out string outName)
	{
		if (!m_LightsComponent)
			return false;
		
		string actionName = GetUIInfo().GetName();
		string state = "OFF";
		if (m_LightsComponent.IsIROn())
			state = "ON";
		
		outName = string.Format("Toggle: %1 [%2]", actionName, state);
		return true;
	}
	
	override bool CanBroadcastScript() 
	{ 
		return false;
	}
}

//------------------------------------------------------------------------------------------------
// Toggle Strobe Action
//------------------------------------------------------------------------------------------------
class ACM_ToggleStrobeAction : ACM_FunctionAction
{
	protected ACM_NVL_StrobeComponent m_StrobeComponent;
	protected InventoryItemComponent m_InventoryItemComp;
	
	override void Init(IEntity pOwnerEntity, GenericComponent pManagerComponent)
	{
		m_StrobeComponent = ACM_NVL_StrobeComponent.Cast(pOwnerEntity.FindComponent(ACM_NVL_StrobeComponent));
		m_InventoryItemComp = InventoryItemComponent.Cast(pOwnerEntity.FindComponent(InventoryItemComponent));
	}
	
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		ACM_NVL_StrobeComponent strobeComponent = ACM_NVL_StrobeComponent.Cast(pOwnerEntity.FindComponent(ACM_NVL_StrobeComponent));
		if (!strobeComponent)
			return;
		
		strobeComponent.RequestToggle();
	}
	
	override bool CanBeShownScript(IEntity user)
	{
		if (!m_InventoryItemComp || !m_InventoryItemComp.GetParentSlot())
			return false;
		
		if (!m_StrobeComponent)
			return false;
		
		ChimeraCharacter character = ChimeraCharacter.Cast(SCR_PlayerController.GetLocalControlledEntity());
		if (!character)
			return false;
		
		CharacterControllerComponent controller = character.GetCharacterController();	
		if (!controller)
			return false;		
		
		if (!controller.GetInspect())
			return false;
		
		BaseWeaponManagerComponent weaponManager = controller.GetWeaponManagerComponent();
		if (!weaponManager)
			return false;
		
		BaseWeaponComponent weaponComp = weaponManager.GetCurrentWeapon();
		if (!weaponComp || weaponComp.GetOwner() != m_InventoryItemComp.GetParentSlot().GetOwner())
			return false;
		
		return true;
	}
	
	override bool GetActionNameScript(out string outName)
	{
		if (!m_StrobeComponent)
			return false;
		
		string actionName = GetUIInfo().GetName();
		string state = "OFF";
		if (m_StrobeComponent.IsActive())
			state = "ON";
		
		outName = string.Format("Toggle: %1 [%2]", actionName, state);
		return true;
	}
	
	override bool CanBroadcastScript() 
	{ 
		return false;
	}
}
