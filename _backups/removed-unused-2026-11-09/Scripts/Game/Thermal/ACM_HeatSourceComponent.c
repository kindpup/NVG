//------------------------------------------------------------------------------------------------
// ACM_HeatSourceComponent.c
// Component that marks an entity as a heat source for thermal imaging
// Spawns a light prefab when thermal vision is active to make entity visible
//------------------------------------------------------------------------------------------------

[ComponentEditorProps(category: "ACM/Thermal", description: "Marks entity as thermal heat source")]
class ACM_HeatSourceComponentClass : ScriptComponentClass
{
}

class ACM_HeatSourceComponent : ScriptComponent
{
	// Heat configuration
	[Attribute("1.0", UIWidgets.Slider, desc: "Base heat intensity (0-1)", category: "Heat Source", params: "0 1 0.05")]
	protected float m_fBaseHeat;
	
	[Attribute("1", UIWidgets.CheckBox, desc: "Heat varies based on activity (movement, engine running)", category: "Heat Source")]
	protected bool m_bDynamicHeat;
	
	[Attribute("0", UIWidgets.CheckBox, desc: "This is a vehicle heat source", category: "Heat Source")]
	protected bool m_bIsVehicle;
	
	[Attribute("100", UIWidgets.Slider, desc: "Detection range in meters", category: "Heat Source", params: "1 500 10")]
	protected float m_fDetectionRange;
	
	[Attribute("BODY", UIWidgets.ComboBox, desc: "Heat source type", category: "Heat Source", enums: ParamEnumArray.FromEnum(EACM_HeatSourceType))]
	protected EACM_HeatSourceType m_eHeatType;
	
	// Light configuration for thermal glow
	[Attribute("1.5", UIWidgets.Slider, desc: "Height offset for heat light", category: "Heat Light", params: "0 3 0.1")]
	protected float m_fLightHeightOffset;
	
	[Attribute("{941426C9D4AECC95}Prefabs/Thermal/ACM_HeatLight.et", UIWidgets.ResourceNamePicker, desc: "Light prefab for thermal glow", "et", category: "Heat Light")]
	protected ResourceName m_HeatLightPrefab;
	
	// Internal state
	protected float m_fCurrentHeat;
	protected bool m_bIsRegistered;
	protected IEntity m_Owner;
	protected IEntity m_HeatLight;
	protected bool m_bLightActive;
	
	// For vehicles
	protected VehicleControllerComponent m_VehicleController;
	
	// For characters
	protected CharacterControllerComponent m_CharController;
	
	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		
		m_Owner = owner;
		m_fCurrentHeat = m_fBaseHeat;
		
		SetEventMask(owner, EntityEvent.INIT | EntityEvent.FRAME);
		
		// Get controllers for dynamic heat
		if (m_bDynamicHeat)
		{
			if (m_bIsVehicle)
			{
				m_VehicleController = VehicleControllerComponent.Cast(owner.FindComponent(VehicleControllerComponent));
			}
			else
			{
				m_CharController = CharacterControllerComponent.Cast(owner.FindComponent(CharacterControllerComponent));
			}
		}
	}
	
	//------------------------------------------------------------------------------------------------
	override void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);
		
		// Register with thermal system
		ACM_ThermalSystem system = ACM_ThermalSystem.GetInstance();
		if (system)
		{
			system.RegisterHeatSource(this);
			m_bIsRegistered = true;
			Print("[ACM_HeatSource] Registered heat source: " + owner, LogLevel.DEBUG);
		}
	}
	
	//------------------------------------------------------------------------------------------------
	override void EOnFrame(IEntity owner, float timeSlice)
	{
		// Update light position if active
		if (m_bLightActive && m_HeatLight && m_Owner)
		{
			vector pos = m_Owner.GetOrigin();
			pos[1] = pos[1] + m_fLightHeightOffset;
			m_HeatLight.SetOrigin(pos);
		}
	}
	
	//------------------------------------------------------------------------------------------------
	void EnableHeatLight()
	{
		if (m_bLightActive)
			return;
		
		if (!m_Owner)
			return;
		
		// Try to spawn light from prefab
		if (!m_HeatLightPrefab.IsEmpty())
		{
			Resource res = Resource.Load(m_HeatLightPrefab);
			if (res && res.IsValid())
			{
				m_HeatLight = GetGame().SpawnEntityPrefabLocal(res);
			}
		}
		
		// Fallback: spawn basic light entity
		if (!m_HeatLight)
		{
			m_HeatLight = GetGame().SpawnEntity(LightEntity);
		}
		
		if (!m_HeatLight)
		{
			Print("[ACM_HeatSource] Failed to spawn heat light!", LogLevel.WARNING);
			return;
		}
		
		// Position at entity
		vector pos = m_Owner.GetOrigin();
		pos[1] = pos[1] + m_fLightHeightOffset;
		m_HeatLight.SetOrigin(pos);
		
		// Enable the light entity
		m_HeatLight.SetFlags(EntityFlags.ACTIVE | EntityFlags.VISIBLE);
		
		// If it's a LightEntity, enable it
		LightEntity lightEnt = LightEntity.Cast(m_HeatLight);
		if (lightEnt)
			lightEnt.SetEnabled(true);
		
		m_bLightActive = true;
		
		Print("[ACM_HeatSource] Heat light enabled for: " + m_Owner, LogLevel.DEBUG);
	}
	
	//------------------------------------------------------------------------------------------------
	void DisableHeatLight()
	{
		if (!m_bLightActive)
			return;
		
		if (m_HeatLight)
		{
			LightEntity lightEnt = LightEntity.Cast(m_HeatLight);
			if (lightEnt)
				lightEnt.SetEnabled(false);
			
			SCR_EntityHelper.DeleteEntityAndChildren(m_HeatLight);
			m_HeatLight = null;
		}
		
		m_bLightActive = false;
		Print("[ACM_HeatSource] Heat light disabled", LogLevel.DEBUG);
	}
	
	//------------------------------------------------------------------------------------------------
	bool IsLightActive()
	{
		return m_bLightActive;
	}
	
	//------------------------------------------------------------------------------------------------
	void ~ACM_HeatSourceComponent()
	{
		DisableHeatLight();
		
		// Unregister from thermal system
		if (m_bIsRegistered)
		{
			ACM_ThermalSystem system = ACM_ThermalSystem.GetInstance();
			if (system)
				system.UnregisterHeatSource(this);
		}
	}
	
	//------------------------------------------------------------------------------------------------
	// Getters
	//------------------------------------------------------------------------------------------------
	IEntity GetOwnerEntity()
	{
		return m_Owner;
	}
	
	float GetCurrentHeat()
	{
		if (!m_bDynamicHeat)
			return m_fBaseHeat;
		
		// Calculate dynamic heat based on activity
		m_fCurrentHeat = m_fBaseHeat;
		
		if (m_bIsVehicle && m_VehicleController)
		{
			// Vehicle heat based on engine state
			if (m_VehicleController.IsEngineOn())
				m_fCurrentHeat = Math.Clamp(m_fBaseHeat + 0.3, 0, 1);
		}
		else if (m_CharController)
		{
			// Character heat based on movement
			float speed = m_CharController.GetMovementVelocity().Length();
			if (speed > 2)
				m_fCurrentHeat = Math.Clamp(m_fBaseHeat + 0.2, 0, 1);
		}
		
		return m_fCurrentHeat;
	}
	
	float GetBaseHeat()
	{
		return m_fBaseHeat;
	}
	
	float GetDetectionRange()
	{
		return m_fDetectionRange;
	}
	
	EACM_HeatSourceType GetHeatType()
	{
		return m_eHeatType;
	}
	
	bool IsVehicle()
	{
		return m_bIsVehicle;
	}
	
	vector GetWorldPosition()
	{
		if (!m_Owner)
			return vector.Zero;
		return m_Owner.GetOrigin();
	}
	
	//------------------------------------------------------------------------------------------------
	// Setters
	//------------------------------------------------------------------------------------------------
	void SetBaseHeat(float heat)
	{
		m_fBaseHeat = Math.Clamp(heat, 0, 1);
	}
}

//------------------------------------------------------------------------------------------------
// Heat source types
//------------------------------------------------------------------------------------------------
enum EACM_HeatSourceType
{
	BODY,		// Living body (character)
	ENGINE,		// Vehicle engine
	EXHAUST,	// Vehicle exhaust
	FIRE,		// Fire/explosion
	MUZZLE,		// Recently fired weapon
	ELECTRONICS	// Electronic equipment
}
