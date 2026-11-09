//------------------------------------------------------------------------------------------------
// ACM_ThermalSystem.c
// Global manager for thermal imaging heat sources
// Enables/disables heat lights when thermal vision is active
//------------------------------------------------------------------------------------------------

class ACM_ThermalSystem
{
	// Singleton instance
	protected static ref ACM_ThermalSystem s_Instance;
	
	// Registered heat sources (component-based)
	protected ref array<ACM_HeatSourceComponent> m_aHeatSources = {};
	
	// Registered entities without component (characters, etc)
	protected ref array<ref ACM_VirtualHeatSource> m_aVirtualHeatSources = {};
	
	// Virtual heat source lights
	protected ref map<IEntity, IEntity> m_mVirtualLights = new map<IEntity, IEntity>();
	
	// Light prefab for virtual heat sources
	protected ResourceName m_VirtualLightPrefab = "{941426C9D4AECC95}Prefabs/Thermal/ACM_HeatLight.et";
	
	// Thermal active state
	protected bool m_bThermalActive;
	
	// Viewer position for range checks
	protected vector m_vViewerPos;
	protected float m_fMaxRange;
	
	//------------------------------------------------------------------------------------------------
	static ACM_ThermalSystem GetInstance()
	{
		if (!s_Instance)
			s_Instance = new ACM_ThermalSystem();
		return s_Instance;
	}
	
	//------------------------------------------------------------------------------------------------
	void RegisterHeatSource(ACM_HeatSourceComponent source)
	{
		if (!source)
			return;
		
		if (m_aHeatSources.Find(source) == -1)
		{
			m_aHeatSources.Insert(source);
			Print("[ACM_ThermalSystem] Registered component heat source. Total: " + m_aHeatSources.Count(), LogLevel.DEBUG);
			
			// If thermal is already active, enable the light
			if (m_bThermalActive)
			{
				float dist = vector.Distance(m_vViewerPos, source.GetWorldPosition());
				if (dist <= m_fMaxRange && dist <= source.GetDetectionRange())
					source.EnableHeatLight();
			}
		}
	}
	
	//------------------------------------------------------------------------------------------------
	void UnregisterHeatSource(ACM_HeatSourceComponent source)
	{
		if (!source)
			return;
		
		int idx = m_aHeatSources.Find(source);
		if (idx != -1)
		{
			source.DisableHeatLight();
			m_aHeatSources.Remove(idx);
		}
	}
	
	//------------------------------------------------------------------------------------------------
	// Register a character/entity as heat source without needing a component
	//------------------------------------------------------------------------------------------------
	void RegisterCharacterHeatSource(IEntity entity, float baseHeat, float range)
	{
		if (!entity)
			return;
		
		// Check if already registered
		foreach (ACM_VirtualHeatSource vhs : m_aVirtualHeatSources)
		{
			if (vhs && vhs.m_Entity == entity)
				return;
		}
		
		ACM_VirtualHeatSource vhs = new ACM_VirtualHeatSource();
		vhs.m_Entity = entity;
		vhs.m_fBaseHeat = baseHeat;
		vhs.m_fRange = range;
		m_aVirtualHeatSources.Insert(vhs);
		
		Print("[ACM_ThermalSystem] Registered virtual heat source. Total: " + m_aVirtualHeatSources.Count(), LogLevel.DEBUG);
		
		// If thermal is already active, create light for this entity
		if (m_bThermalActive)
		{
			float dist = vector.Distance(m_vViewerPos, entity.GetOrigin());
			if (dist <= m_fMaxRange && dist <= range)
				CreateVirtualLight(entity, baseHeat);
		}
	}
	
	//------------------------------------------------------------------------------------------------
	void UnregisterCharacterHeatSource(IEntity entity)
	{
		if (!entity)
			return;
		
		// Remove virtual light if exists
		RemoveVirtualLight(entity);
		
		for (int i = m_aVirtualHeatSources.Count() - 1; i >= 0; i--)
		{
			if (m_aVirtualHeatSources[i] && m_aVirtualHeatSources[i].m_Entity == entity)
			{
				m_aVirtualHeatSources.Remove(i);
				break;
			}
		}
	}
	
	//------------------------------------------------------------------------------------------------
	protected void CreateVirtualLight(IEntity entity, float heat)
	{
		if (!entity)
			return;
		
		// Don't create duplicate
		if (m_mVirtualLights.Contains(entity))
			return;
		
		IEntity light;
		
		// Try to spawn from prefab
		if (!m_VirtualLightPrefab.IsEmpty())
		{
			Resource res = Resource.Load(m_VirtualLightPrefab);
			if (res && res.IsValid())
			{
				light = GetGame().SpawnEntityPrefabLocal(res);
			}
		}
		
		// Fallback: spawn basic light entity
		if (!light)
		{
			light = GetGame().SpawnEntity(LightEntity);
		}
		
		if (!light)
		{
			Print("[ACM_ThermalSystem] Failed to spawn virtual heat light!", LogLevel.WARNING);
			return;
		}
		
		// Position at entity center (chest level inside body)
		vector pos = entity.GetOrigin();
		pos[1] = pos[1] + 1.0;
		light.SetOrigin(pos);
		
		// Enable the light
		light.SetFlags(EntityFlags.ACTIVE | EntityFlags.VISIBLE);
		
		LightEntity lightEnt = LightEntity.Cast(light);
		if (lightEnt)
			lightEnt.SetEnabled(true);
		
		m_mVirtualLights.Insert(entity, light);
		
		Print("[ACM_ThermalSystem] Created virtual heat light for: " + entity, LogLevel.DEBUG);
	}
	
	//------------------------------------------------------------------------------------------------
	protected void RemoveVirtualLight(IEntity entity)
	{
		if (!entity)
			return;
		
		IEntity light;
		if (m_mVirtualLights.Find(entity, light))
		{
			if (light)
			{
				LightEntity lightEnt = LightEntity.Cast(light);
				if (lightEnt)
					lightEnt.SetEnabled(false);
				
				SCR_EntityHelper.DeleteEntityAndChildren(light);
			}
			m_mVirtualLights.Remove(entity);
		}
	}
	
	//------------------------------------------------------------------------------------------------
	protected void UpdateVirtualLightPositions()
	{
		for (int i = 0; i < m_mVirtualLights.Count(); i++)
		{
			IEntity entity = m_mVirtualLights.GetKey(i);
			IEntity light = m_mVirtualLights.GetElement(i);
			
			if (!entity || !light)
				continue;
			
			vector pos = entity.GetOrigin();
			pos[1] = pos[1] + 1.0;
			light.SetOrigin(pos);
		}
	}
	
	//------------------------------------------------------------------------------------------------
	int GetHeatSourceCount()
	{
		return m_aHeatSources.Count() + m_aVirtualHeatSources.Count();
	}
	
	//------------------------------------------------------------------------------------------------
	bool IsThermalActive()
	{
		return m_bThermalActive;
	}
	
	//------------------------------------------------------------------------------------------------
	// Enable thermal highlighting for heat sources in range
	//------------------------------------------------------------------------------------------------
	void EnableThermalHighlighting(vector viewerPos, float maxRange)
	{
		m_bThermalActive = true;
		m_vViewerPos = viewerPos;
		m_fMaxRange = maxRange;
		
		Print("[ACM_ThermalSystem] EnableThermalHighlighting - range: " + maxRange, LogLevel.DEBUG);
		Print("[ACM_ThermalSystem] Component sources: " + m_aHeatSources.Count() + ", Virtual sources: " + m_aVirtualHeatSources.Count(), LogLevel.DEBUG);
		
		// Enable lights on component-based heat sources
		foreach (ACM_HeatSourceComponent source : m_aHeatSources)
		{
			if (!source)
				continue;
			
			float dist = vector.Distance(viewerPos, source.GetWorldPosition());
			if (dist <= maxRange && dist <= source.GetDetectionRange())
			{
				source.EnableHeatLight();
			}
		}
		
		// Create lights for virtual heat sources (characters without component)
		for (int i = m_aVirtualHeatSources.Count() - 1; i >= 0; i--)
		{
			ACM_VirtualHeatSource vhs = m_aVirtualHeatSources[i];
			if (!vhs || !vhs.m_Entity)
			{
				m_aVirtualHeatSources.Remove(i);
				continue;
			}
			
			float dist = vector.Distance(viewerPos, vhs.m_Entity.GetOrigin());
			if (dist <= maxRange && dist <= vhs.m_fRange)
			{
				CreateVirtualLight(vhs.m_Entity, vhs.m_fBaseHeat);
			}
		}
		
		// Start update loop for light positions
		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().CallLater(UpdateLoop, 100, true);
	}
	
	//------------------------------------------------------------------------------------------------
	void DisableThermalHighlighting()
	{
		m_bThermalActive = false;
		
		// Stop update loop
		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().Remove(UpdateLoop);
		
		// Disable lights on component-based heat sources
		foreach (ACM_HeatSourceComponent source : m_aHeatSources)
		{
			if (source)
				source.DisableHeatLight();
		}
		
		// Remove all virtual lights
		for (int i = m_mVirtualLights.Count() - 1; i >= 0; i--)
		{
			IEntity light = m_mVirtualLights.GetElement(i);
			if (light)
			{
				LightEntity lightEnt = LightEntity.Cast(light);
				if (lightEnt)
					lightEnt.SetEnabled(false);
				
				SCR_EntityHelper.DeleteEntityAndChildren(light);
			}
		}
		m_mVirtualLights.Clear();
		
		Print("[ACM_ThermalSystem] Disabled thermal highlighting", LogLevel.DEBUG);
	}
	
	//------------------------------------------------------------------------------------------------
	protected void UpdateLoop()
	{
		if (!m_bThermalActive)
			return;
		
		// Update virtual light positions
		UpdateVirtualLightPositions();
	}
	
	//------------------------------------------------------------------------------------------------
	// Check if entity is a heat source
	//------------------------------------------------------------------------------------------------
	bool IsEntityHeatSource(IEntity entity)
	{
		if (!entity || !m_bThermalActive)
			return false;
		
		// Check component sources
		foreach (ACM_HeatSourceComponent source : m_aHeatSources)
		{
			if (source && source.GetOwnerEntity() == entity)
				return true;
		}
		
		// Check virtual sources
		foreach (ACM_VirtualHeatSource vhs : m_aVirtualHeatSources)
		{
			if (vhs && vhs.m_Entity == entity)
				return true;
		}
		
		return false;
	}
	
	//------------------------------------------------------------------------------------------------
	// Get heat intensity for entity (0-1)
	//------------------------------------------------------------------------------------------------
	float GetEntityHeatIntensity(IEntity entity)
	{
		if (!entity)
			return 0;
		
		// Check component sources
		foreach (ACM_HeatSourceComponent source : m_aHeatSources)
		{
			if (source && source.GetOwnerEntity() == entity)
				return source.GetCurrentHeat();
		}
		
		// Check virtual sources
		foreach (ACM_VirtualHeatSource vhs : m_aVirtualHeatSources)
		{
			if (vhs && vhs.m_Entity == entity)
				return vhs.m_fBaseHeat;
		}
		
		return 0;
	}
}

//------------------------------------------------------------------------------------------------
// Virtual heat source for entities without ACM_HeatSourceComponent
//------------------------------------------------------------------------------------------------
class ACM_VirtualHeatSource
{
	IEntity m_Entity;
	float m_fBaseHeat;
	float m_fRange;
}
