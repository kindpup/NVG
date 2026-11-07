// ACM IR Management System
// Centralized controller for infrared light source visibility
// Self-initializing singleton - does not require world registration
// Compatible with NV-System mod and other NVG mods - polls for external NVG state

class ACM_IRSystem
{
	protected ref array<ACM_IRLightSource> m_aRegisteredSources = {};
	protected ref array<ACM_IRLaser> m_aRegisteredLasers = {};
	protected ref array<ACM_NVL_LightsComponent> m_aRegisteredNVLLightsComponents = {};
	protected ref array<ACM_NVL_StrobeComponent> m_aRegisteredNVLStrobeComponents = {};
	protected ref array<LightEntity> m_aCompatForcedExternalIRLights = {};
	
	protected bool m_bIRVisible;
	protected bool m_bACMNVActive;       // ACM NVG/Thermal active
	protected bool m_bExternalNVActive;  // External NV system (NV-System mod) active
	protected bool m_bPollingActive;
	// FPS patch: external NV compatibility scanning walks inventory/attachments. Keep it off by default.
	protected static const bool ENABLE_EXTERNAL_NV_POLLING = false;
	protected static const int EXTERNAL_NV_POLL_INTERVAL_MS = 1500;
	protected static const bool ENABLE_OPTIONAL_EXTERNAL_IR_COMPAT = false;
	protected static const int MAX_EXTERNAL_IR_DEPTH = 12;
	protected static ref array<string> s_aCompatPowerMethodNames = {
		"IsTurnedOn",
		"GetIsTurnedOn",
		"IsPoweredOn",
		"GetIsPoweredOn",
		"IsDeviceOn",
		"GetDeviceState",
		"IsOn",
		"GetIsOn"
	};
	protected static ref ACM_IRSystem s_Instance;
	
	static ACM_IRSystem GetInstance()
	{
		if (!s_Instance)
		{
			s_Instance = new ACM_IRSystem();
		}
		return s_Instance;
	}
	
	void ACM_IRSystem()
	{
		m_aRegisteredSources = new array<ACM_IRLightSource>();
		m_aRegisteredLasers = new array<ACM_IRLaser>();
		m_aRegisteredNVLLightsComponents = new array<ACM_NVL_LightsComponent>();
		m_aRegisteredNVLStrobeComponents = new array<ACM_NVL_StrobeComponent>();
		m_aCompatForcedExternalIRLights = new array<LightEntity>();
		
		// FPS patch: do not start external NV inventory polling unless explicitly re-enabled.
		if (ENABLE_EXTERNAL_NV_POLLING && !IsDedicatedServer())
			StartPolling();
	}
	
	//------------------------------------------------------------------------------------------------
	// Check if running on dedicated server (no local player controller possible)
	//------------------------------------------------------------------------------------------------
	protected bool IsDedicatedServer()
	{
		// On dedicated server, there's no player controller
		if (!GetGame())
			return true;
		
		// Check if this is a dedicated server by checking for player controller
		// Dedicated servers run replication but have no local player
		if (Replication.IsRunning() && Replication.IsServer() && !GetGame().GetPlayerController())
			return true;
		
		return false;
	}
	
	//------------------------------------------------------------------------------------------------
	// Polling system to detect external NV mods (NV-System, etc)
	//------------------------------------------------------------------------------------------------
	protected void StartPolling()
	{
		if (m_bPollingActive)
			return;
		
		// Safety check - don't poll on dedicated servers
		if (IsDedicatedServer())
			return;
		
		if (!GetGame() || !GetGame().GetCallqueue())
			return;
		
		if (!ENABLE_EXTERNAL_NV_POLLING)
			return;
		
		m_bPollingActive = true;
		GetGame().GetCallqueue().CallLater(PollExternalNVSystems, EXTERNAL_NV_POLL_INTERVAL_MS, true);
	}
	
	protected void StopPolling()
	{
		if (!m_bPollingActive)
			return;
		
		m_bPollingActive = false;
		
		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().Remove(PollExternalNVSystems);
	}
	
	protected void PollExternalNVSystems()
	{
		if (!ENABLE_EXTERNAL_NV_POLLING)
		{
			StopPolling();
			m_bExternalNVActive = false;
			UpdateIRVisibility();
			return;
		}
		
		// Safety check - stop polling if we somehow ended up on a dedicated server
		if (IsDedicatedServer())
		{
			StopPolling();
			return;
		}
		
		IEntity player = SCR_PlayerController.GetLocalControlledEntity();
		if (!player)
			return;
		
		bool externalActive = CheckForActiveExternalNVG(player);
		SyncOptionalExternalIRLights(player, m_bACMNVActive || externalActive);
		
		// Only update if state changed (reduces unnecessary updates)
		if (externalActive != m_bExternalNVActive)
		{
			m_bExternalNVActive = externalActive;
			UpdateIRVisibility();
		}
	}
	
	//------------------------------------------------------------------------------------------------
	// Check if any external NVG system (NV-System, RHS, etc) is active
	//------------------------------------------------------------------------------------------------
	protected bool CheckForActiveExternalNVG(IEntity player)
	{
		if (!player)
			return false;
		
		EquipedLoadoutStorageComponent loadout = EquipedLoadoutStorageComponent.Cast(
			player.FindComponent(EquipedLoadoutStorageComponent));
		
		// Check helmet and helmet attachments
		if (loadout)
		{
			IEntity helmet = loadout.GetClothFromArea(LoadoutHeadCoverArea);
			if (helmet)
			{
				if (CheckEntityForActiveNVG(helmet))
					return true;
				
				// Check helmet's storage for attached items (NVGs on mount)
				if (CheckStorageForActiveNVG(helmet))
					return true;
			}
		}
		
		// Check weapon and weapon attachments (for weapon-mounted NV scopes)
		BaseWeaponManagerComponent weaponMgr = BaseWeaponManagerComponent.Cast(
			player.FindComponent(BaseWeaponManagerComponent));
		
		if (weaponMgr)
		{
			BaseWeaponComponent currentWeapon = weaponMgr.GetCurrentWeapon();
			if (currentWeapon)
			{
				IEntity weaponEntity = currentWeapon.GetOwner();
				if (weaponEntity)
				{
					if (CheckEntityForActiveNVG(weaponEntity))
						return true;
					
					// Check weapon attachments
					if (CheckStorageForActiveNVG(weaponEntity))
						return true;
					
					// Check weapon children (attached entities)
					if (CheckChildrenForActiveNVG(weaponEntity))
						return true;
				}
			}
		}
		
		// Check direct children of player (items parented directly)
		if (CheckChildrenForActiveNVG(player))
			return true;
		
		return false;
	}
	
	//------------------------------------------------------------------------------------------------
	// Check storage components for active NVG
	//------------------------------------------------------------------------------------------------
	// Max recursion depth to prevent stack overflow
	protected static const int MAX_STORAGE_DEPTH = 8;
	
	protected bool CheckStorageForActiveNVG(IEntity entity, int depth = 0)
	{
		if (!entity)
			return false;
		
		// Prevent stack overflow from deep recursion
		if (depth >= MAX_STORAGE_DEPTH)
			return false;
		
		// Check ClothNodeStorageComponent (helmet mounts)
		ClothNodeStorageComponent clothStorage = ClothNodeStorageComponent.Cast(
			entity.FindComponent(ClothNodeStorageComponent));
		
		if (clothStorage)
		{
			array<IEntity> items = {};
			clothStorage.GetAll(items);
			
			foreach (IEntity item : items)
			{
				if (!item)
					continue;
				
				if (CheckEntityForActiveNVG(item))
					return true;
				
				// Recurse into item's storage with depth limit
				if (CheckStorageForActiveNVG(item, depth + 1))
					return true;
			}
		}
		
		// Check BaseInventoryStorageComponent (general storage)
		BaseInventoryStorageComponent invStorage = BaseInventoryStorageComponent.Cast(
			entity.FindComponent(BaseInventoryStorageComponent));
		
		if (invStorage)
		{
			array<IEntity> items = {};
			invStorage.GetAll(items);
			
			foreach (IEntity item : items)
			{
				if (!item)
					continue;
				
				if (CheckEntityForActiveNVG(item))
					return true;
				
				// Recurse into item's storage with depth limit
				if (CheckStorageForActiveNVG(item, depth + 1))
					return true;
			}
		}
		
		// Check AttachmentSlotComponent (weapon attachments)
		array<Managed> attachmentSlots = {};
		entity.FindComponents(AttachmentSlotComponent, attachmentSlots);
		
		foreach (Managed slotManaged : attachmentSlots)
		{
			AttachmentSlotComponent slot = AttachmentSlotComponent.Cast(slotManaged);
			if (!slot)
				continue;
			
			IEntity attachment = slot.GetAttachedEntity();
			if (!attachment)
				continue;
			
			if (CheckEntityForActiveNVG(attachment))
				return true;
			
			// Recurse into attachment's storage with depth limit
			if (CheckStorageForActiveNVG(attachment, depth + 1))
				return true;
		}
		
		return false;
	}
	
	//------------------------------------------------------------------------------------------------
	// Check entity children for active NVG
	//------------------------------------------------------------------------------------------------
	protected static const int MAX_CHILDREN_DEPTH = 10;
	
	protected bool CheckChildrenForActiveNVG(IEntity entity, int depth = 0)
	{
		if (!entity)
			return false;
		
		// Prevent stack overflow from deep recursion
		if (depth >= MAX_CHILDREN_DEPTH)
			return false;
		
		IEntity child = entity.GetChildren();
		while (child)
		{
			if (CheckEntityForActiveNVG(child))
				return true;
			
			// Recurse into children with depth limit
			if (CheckChildrenForActiveNVG(child, depth + 1))
				return true;
			
			child = child.GetSibling();
		}
		
		return false;
	}
	
	//------------------------------------------------------------------------------------------------
	// Check a single entity for active external NVG
	//------------------------------------------------------------------------------------------------
	protected bool CheckEntityForActiveNVG(IEntity entity)
	{
		if (!entity)
			return false;
		
		// Find any SCR_GadgetComponent
		SCR_GadgetComponent gadget = SCR_GadgetComponent.Cast(
			entity.FindComponent(SCR_GadgetComponent));
		
		if (!gadget)
			return false;
		
		// Skip our own ACM components by checking class name
		string className = gadget.ClassName();
		if (className.Contains("ACM_"))
			return false;
		
		// Check if it's night vision type
		if (gadget.GetType() != EGadgetType.NIGHT_VISION)
			return false;
		
		// Check if active using IsToggledOn()
		return gadget.IsToggledOn();
	}

	//------------------------------------------------------------------------------------------------
	// Optional runtime compatibility for external IR emitters that can be identified without
	// hard references. This stays dormant when those classes are not loaded.
	//------------------------------------------------------------------------------------------------
	protected void SyncOptionalExternalIRLights(IEntity player, bool shouldBeVisible)
	{
		if (!ENABLE_OPTIONAL_EXTERNAL_IR_COMPAT)
			return;

		if (!player)
			return;

		SyncOptionalExternalIRLightsRecursive(player, shouldBeVisible, 0);
	}

	protected void SyncOptionalExternalIRLightsRecursive(IEntity entity, bool shouldBeVisible, int depth)
	{
		if (!entity)
			return;

		if (depth >= MAX_EXTERNAL_IR_DEPTH)
			return;

		ApplyOptionalExternalIRManagerState(entity, shouldBeVisible);
		ApplyOptionalExternalIRLightState(entity, shouldBeVisible);

		IEntity child = entity.GetChildren();
		while (child)
		{
			SyncOptionalExternalIRLightsRecursive(child, shouldBeVisible, depth + 1);
			child = child.GetSibling();
		}

		ClothNodeStorageComponent clothStorage = ClothNodeStorageComponent.Cast(entity.FindComponent(ClothNodeStorageComponent));
		if (clothStorage)
		{
			array<IEntity> clothItems = {};
			clothStorage.GetAll(clothItems);
			foreach (IEntity item : clothItems)
			{
				if (item)
					SyncOptionalExternalIRLightsRecursive(item, shouldBeVisible, depth + 1);
			}
		}

		BaseInventoryStorageComponent invStorage = BaseInventoryStorageComponent.Cast(entity.FindComponent(BaseInventoryStorageComponent));
		if (invStorage)
		{
			array<IEntity> storedItems = {};
			invStorage.GetAll(storedItems);
			foreach (IEntity item : storedItems)
			{
				if (item)
					SyncOptionalExternalIRLightsRecursive(item, shouldBeVisible, depth + 1);
			}
		}

		array<Managed> attachmentSlots = {};
		entity.FindComponents(AttachmentSlotComponent, attachmentSlots);
		foreach (Managed slotManaged : attachmentSlots)
		{
			AttachmentSlotComponent slot = AttachmentSlotComponent.Cast(slotManaged);
			if (!slot)
				continue;

			IEntity attachment = slot.GetAttachedEntity();
			if (attachment)
				SyncOptionalExternalIRLightsRecursive(attachment, shouldBeVisible, depth + 1);
		}
	}

	protected void ApplyOptionalExternalIRManagerState(IEntity entity, bool shouldBeVisible)
	{
		BaseLightManagerComponent lightManager = BaseLightManagerComponent.Cast(entity.FindComponent(BaseLightManagerComponent));
		if (!lightManager)
			return;

		if (!EntityHasOptionalExternalRHSComponents(entity))
			return;

		array<BaseLightSlot> lightSlots = {};
		if (lightManager.GetLights(lightSlots) <= 0)
			return;

		foreach (BaseLightSlot slot : lightSlots)
		{
			if (!slot)
				continue;

			LightEntity light = LightEntity.Cast(slot.GetLightEntity());
			if (!light)
				continue;

			if (!IsOptionalExternalIRLight(light))
				continue;

			bool slotActive;
			bool hasExplicitPowerState = TryGetOptionalExternalDevicePowered(entity, slotActive);
			if (!hasExplicitPowerState)
				slotActive = lightManager.GetLightsState(slot.GetLightType(), slot.GetLightSide());

			if (!slotActive || !slot.IsLightFunctional())
			{
				ApplyOptionalExternalIRLightState(light, false);
				continue;
			}

			ApplyOptionalExternalIRLightState(light, shouldBeVisible && slotActive);
		}
	}

	protected bool TryGetOptionalExternalDevicePowered(IEntity entity, out bool isPowered)
	{
		isPowered = false;

		if (!entity || !GetGame())
			return false;

		ScriptModule scriptModule = GetGame().GetScriptModule();
		if (!scriptModule)
			return false;

		array<Managed> components = {};
		entity.FindComponents(ScriptComponent, components);
		foreach (Managed componentManaged : components)
		{
			ScriptComponent component = ScriptComponent.Cast(componentManaged);
			if (!component)
				continue;

			string componentType = component.Type().ToString();
			componentType.ToUpper();
			if (!componentType.Contains("RHS_"))
				continue;

			foreach (string methodName : s_aCompatPowerMethodNames)
			{
				bool poweredResult = false;
				if (scriptModule.Call(component, methodName, false, poweredResult))
				{
					isPowered = poweredResult;
					return true;
				}
			}
		}

		return false;
	}

	protected void ApplyOptionalExternalIRLightState(IEntity entity, bool shouldBeVisible)
	{
		LightEntity light = LightEntity.Cast(entity);
		if (!light)
			return;

		if (!IsOptionalExternalIRLight(light))
			return;

		bool forcedByCompat = m_aCompatForcedExternalIRLights.Contains(light);

		if (shouldBeVisible)
		{
			if (!light.IsEnabled())
			{
				light.SetEnabled(true);
				if (!forcedByCompat)
					m_aCompatForcedExternalIRLights.Insert(light);
			}

			entity.SetFlags(EntityFlags.VISIBLE);
			return;
		}

		entity.ClearFlags(EntityFlags.VISIBLE);

		if (forcedByCompat)
		{
			light.SetEnabled(false);
			m_aCompatForcedExternalIRLights.RemoveItem(light);
		}
	}

	protected bool IsOptionalExternalIRLight(LightEntity light)
	{
		if (!light)
			return false;

		string entityType = light.Type().ToString();
		string entityName = light.GetName();
		string prefabType;
		string prefabInfo;

		Managed prefabData = light.GetPrefabData();
		if (prefabData)
		{
			prefabType = prefabData.Type().ToString();
			prefabInfo = prefabData.ToString();
		}

		bool looksExternalLight = entityType.Contains("RHS_LightEntity") || prefabType.Contains("RHS_LightEntityClass") || prefabInfo.Contains("RHS_LightEntityClass");
		if (!looksExternalLight)
			return false;

		string combinedInfo = entityType + " " + entityName + " " + prefabType + " " + prefabInfo;
		combinedInfo.ToUpper();

		IEntity owner = light.GetParent();
		if (owner && EntityHasOptionalExternalRHSComponents(owner))
			return true;

		return combinedInfo.Contains("_IR_")
			|| combinedInfo.Contains(" IR ")
			|| combinedInfo.Contains("IRLIGHT")
			|| combinedInfo.Contains("MS2000")
			|| combinedInfo.Contains("HELSTAR")
			|| combinedInfo.Contains("ANPEQ")
			|| combinedInfo.Contains("PERST");
	}

	protected bool EntityHasOptionalExternalRHSComponents(IEntity entity)
	{
		if (!entity)
			return false;

		string entityType = entity.Type().ToString();
		entityType.ToUpper();
		if (entityType.Contains("RHS_"))
			return true;

		array<Managed> components = {};
		entity.FindComponents(ScriptComponent, components);
		foreach (Managed componentManaged : components)
		{
			ScriptComponent component = ScriptComponent.Cast(componentManaged);
			if (!component)
				continue;

			string componentType = component.Type().ToString();
			componentType.ToUpper();
			if (componentType.Contains("RHS_"))
				return true;
		}

		return false;
	}
	
	//------------------------------------------------------------------------------------------------
	// Called by ACM NVG/Thermal components
	//------------------------------------------------------------------------------------------------
	void SetIRVisibility(bool visible)
	{
		// Removed verbose logging - only log warnings/errors
		m_bACMNVActive = visible;
		UpdateIRVisibility();
	}
	
	//------------------------------------------------------------------------------------------------
	// Update actual visibility based on any NV system being active
	//------------------------------------------------------------------------------------------------
	protected void UpdateIRVisibility()
	{
		bool shouldBeVisible = m_bACMNVActive || m_bExternalNVActive;
		
		// Removed verbose logging - only log warnings/errors
		
		// No change needed
		if (shouldBeVisible == m_bIRVisible)
		{
			// Removed verbose logging - only log warnings/errors
			return;
		}
		
		m_bIRVisible = shouldBeVisible;
		
		// Removed verbose logging - only log warnings/errors
		
		// Clean up null references and notify valid ones
		for (int i = m_aRegisteredSources.Count() - 1; i >= 0; i--)
		{
			ACM_IRLightSource src = m_aRegisteredSources[i];
			if (!src)
			{
				m_aRegisteredSources.Remove(i);
				continue;
			}
			src.ToggleIRVisibility(m_bIRVisible);
		}
		
		for (int i = m_aRegisteredLasers.Count() - 1; i >= 0; i--)
		{
			ACM_IRLaser laser = m_aRegisteredLasers[i];
			if (!laser)
			{
				m_aRegisteredLasers.Remove(i);
				continue;
			}
			// Removed verbose logging - only log warnings/errors
			laser.OnNVGStateChanged(m_bIRVisible);
		}
		
		// Removed verbose logging - only log warnings/errors
		
		for (int i = m_aRegisteredNVLLightsComponents.Count() - 1; i >= 0; i--)
		{
			ACM_NVL_LightsComponent lights = m_aRegisteredNVLLightsComponents[i];
			if (!lights)
			{
				m_aRegisteredNVLLightsComponents.Remove(i);
				continue;
			}
			lights.OnNVGStateChanged(m_bIRVisible);
		}
		
		for (int i = m_aRegisteredNVLStrobeComponents.Count() - 1; i >= 0; i--)
		{
			ACM_NVL_StrobeComponent strobe = m_aRegisteredNVLStrobeComponents[i];
			if (!strobe)
			{
				m_aRegisteredNVLStrobeComponents.Remove(i);
				continue;
			}
			strobe.OnNVGStateChanged(m_bIRVisible);
		}
	}
	
	bool IsIRVisible() { return m_bIRVisible; }
	bool IsACMNVActive() { return m_bACMNVActive; }
	bool IsExternalNVActive() { return m_bExternalNVActive; }
	
	void TrackLaser(ACM_IRLaser laser)
	{
		if (!laser)
			return;
		
		if (!m_aRegisteredLasers.Contains(laser))
		{
			m_aRegisteredLasers.Insert(laser);
			laser.OnNVGStateChanged(m_bIRVisible);
		}
	}
	
	void UntrackLaser(ACM_IRLaser laser)
	{
		if (!laser)
			return;
		
		m_aRegisteredLasers.RemoveItem(laser);
	}
	
	void AddSource(ACM_IRLightSource source)
	{
		if (!source)
			return;
		
		if (!m_aRegisteredSources.Contains(source))
			m_aRegisteredSources.Insert(source);
	}
	
	void RemoveSource(ACM_IRLightSource source)
	{
		if (!source)
			return;
		
		m_aRegisteredSources.RemoveItem(source);
	}
	
	void AddNVLLightsComponent(ACM_NVL_LightsComponent lights)
	{
		if (!lights)
			return;
		
		if (!m_aRegisteredNVLLightsComponents.Contains(lights))
		{
			m_aRegisteredNVLLightsComponents.Insert(lights);
			lights.OnNVGStateChanged(m_bIRVisible);
		}
	}
	
	void RemoveNVLLightsComponent(ACM_NVL_LightsComponent lights)
	{
		if (!lights)
			return;
		
		m_aRegisteredNVLLightsComponents.RemoveItem(lights);
	}
	
	void AddNVLStrobeComponent(ACM_NVL_StrobeComponent strobe)
	{
		if (!strobe)
			return;
		
		if (!m_aRegisteredNVLStrobeComponents.Contains(strobe))
		{
			m_aRegisteredNVLStrobeComponents.Insert(strobe);
			strobe.OnNVGStateChanged(m_bIRVisible);
		}
	}
	
	void RemoveNVLStrobeComponent(ACM_NVL_StrobeComponent strobe)
	{
		if (!strobe)
			return;
		
		m_aRegisteredNVLStrobeComponents.RemoveItem(strobe);
	}
}
