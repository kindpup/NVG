//------------------------------------------------------------------------------------------------
// ACM_NVL_StrobeComponent.c
// Strobe light system with NVG integration
// IR strobes only visible when NVG is active
// Part of ACM Night Vision and Lights mod
//------------------------------------------------------------------------------------------------

[ComponentEditorProps(category: "ACM/Lights", description: "Self-contained strobe effect with configurable speed and NVG integration")]
class ACM_NVL_StrobeComponentClass : ScriptComponentClass {}

class ACM_NVL_StrobeComponent : ScriptComponent
{
	[Attribute("", UIWidgets.Auto, desc: "Light sources to strobe")]
	protected ref array<ref ACM_NVL_LightSource> m_aSources;

	[Attribute("0.08 0.08 0.08 0.6", UIWidgets.EditBox, desc: "Strobe pattern: alternating ON/OFF times in seconds", category: "Timing")]
	protected string m_sStrobePattern;

	[Attribute("0", UIWidgets.CheckBox, desc: "Enable strobe by default")]
	protected bool m_bEnabledByDefault;

	[Attribute("ACMStrobe", UIWidgets.EditBox, desc: "Input action name to toggle strobe")]
	protected string m_sToggleAction;
	
	// Brightness settings
	[Attribute("3.0", UIWidgets.Slider, desc: "Default strobe brightness (LV)", params: "0.1 6 0.1")]
	protected float m_fDefaultBrightness;
	
	[Attribute("0.1", UIWidgets.Slider, desc: "Minimum brightness (LV)", params: "0.01 1 0.01")]
	protected float m_fMinBrightness;
	
	[Attribute("6.0", UIWidgets.Slider, desc: "Maximum brightness (LV)", params: "1 10 0.5")]
	protected float m_fMaxBrightness;
	
	[Attribute("0.2", UIWidgets.Slider, desc: "Brightness adjustment step", params: "0.01 1 0.01")]
	protected float m_fBrightnessStep;

	protected IEntity m_Owner;
	protected bool m_bStrobeEnabled;
	protected bool m_bLightState;

	protected ref map<ACM_NVL_LightSource, LightEntity> m_mLights = new map<ACM_NVL_LightSource, LightEntity>();
	protected ParametricMaterialInstanceComponent m_EmissiveMat;
	protected SoundComponent m_SoundComp;
	protected bool m_bLightsSpawned;
	protected bool m_bSpawnScheduled;

	protected ref array<float> m_aPatternTimes = new array<float>();
	protected int m_iCurrentPatternIndex;

	protected bool m_bActionListenerRegistered;
	protected bool m_bNVGActive;
	protected bool m_bInitialized;
	protected bool m_bIRCoverRemoved;  // When true, IR strobe becomes visible strobe
	// FPS patch: cap action-listener retries.
	protected int m_iActionListenerRetryCount;
	protected static const int MAX_ACTION_LISTENER_RETRIES = 50;
	
	// Current brightness and color
	protected float m_fCurrentBrightness;
	protected int m_iCurrentColorIndex;
	
	// Network sync flag
	protected bool m_bSuppressNetworkSync;
	
	// Available light colors (same as LightsComponent)
	protected static ref array<vector> s_aLightColors = {
		"1 1 1",       // White
		"1 0 0",       // Red
		"0 1 0",       // Green
		"0 0 1",       // Blue
		"1 1 0",       // Yellow
		"1 0.5 0",     // Orange
		"0 1 1",       // Cyan
		"1 0 1"        // Magenta
	};
	
	protected static ref array<string> s_aColorNames = {
		"White",
		"Red",
		"Green",
		"Blue",
		"Yellow",
		"Orange",
		"Cyan",
		"Magenta"
	};

	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		
		if (SCR_Global.IsEditMode())
			return;

		// ACM DS SAFETY: dedicated servers do not need local NV/light visuals.
		if (Replication.IsRunning() && Replication.IsServer() && !GetGame().GetPlayerController())
			return;
		m_Owner = owner;
		SetEventMask(owner, EntityEvent.INIT);
	}

	override void EOnInit(IEntity owner)
	{
		m_Owner = owner;
		
		if (SCR_Global.IsEditMode())
			return;

		// ACM DS SAFETY: dedicated servers do not need local NV/light visuals.
		if (Replication.IsRunning() && Replication.IsServer() && !GetGame().GetPlayerController())
			return;
		// Initialize brightness
		m_fCurrentBrightness = m_fDefaultBrightness;
		m_iCurrentColorIndex = 0;
		
		m_SoundComp = SoundComponent.Cast(owner.FindComponent(SoundComponent));
		m_EmissiveMat = ParametricMaterialInstanceComponent.Cast(owner.FindComponent(ParametricMaterialInstanceComponent));
		
		// Defer initialization to allow replication to settle
		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().CallLater(DeferredInit, 100, false);
	}

	protected void DeferredInit()
	{
		if (m_bInitialized)
			return;

		// ACM DS SAFETY: skip local strobe setup on dedicated servers.
		if (Replication.IsRunning() && Replication.IsServer() && !GetGame().GetPlayerController())
		{
			m_bInitialized = true;
			return;
		}
		
		if (!m_Owner)
			return;
		
		m_bInitialized = true;
		
		ParseStrobePattern();
		
		if (m_aSources && !m_aSources.IsEmpty() && !m_bLightsSpawned && !m_bSpawnScheduled)
		{
			m_bSpawnScheduled = true;
			SpawnLights();
		}
		
		m_bLightState = false;
		m_iCurrentPatternIndex = 0;
		
		// Only register inputs on client
		if (!Replication.IsRunning() || !Replication.IsServer())
		{
			if (GetGame() && GetGame().GetCallqueue())
				GetGame().GetCallqueue().CallLater(RegisterActionListeners, 100, false);
		}
		
		// Register with IR system
		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().CallLater(RegisterWithIRSystem, 150, false);
		
		// Check if enabled by default (only on authority)
		if (m_bEnabledByDefault && IsLocalPlayerOwner())
			RequestToggleStrobe();
	}
	
	protected void RegisterWithIRSystem()
	{
		ACM_IRSystem sys = ACM_IRSystem.GetInstance();
		if (sys)
			sys.AddNVLStrobeComponent(this);
	}
	
	protected void UnregisterFromIRSystem()
	{
		ACM_IRSystem sys = ACM_IRSystem.GetInstance();
		if (sys)
			sys.RemoveNVLStrobeComponent(this);
	}

	protected void SpawnLights()
	{
		if (m_bLightsSpawned)
			return;
		
		if (!m_Owner)
		{
			m_bSpawnScheduled = false;
			return;
		}
		
		if (!m_aSources)
		{
			m_bSpawnScheduled = false;
			return;
		}

		foreach (ACM_NVL_LightSource src : m_aSources)
		{
			if (!src)
				continue;
			
			ResourceName prefab = src.GetPrefab();
			if (prefab.IsEmpty())
				continue;
			
			Resource res = Resource.Load(prefab);
			if (!res || !res.IsValid())
				continue;
			
			LightEntity light = LightEntity.Cast(GetGame().SpawnEntityPrefabLocal(res));
			if (!light)
				continue;
			
			PointInfo offset = src.GetOffset();
			if (!offset)
			{
				SCR_EntityHelper.DeleteEntityAndChildren(light);
				continue;
			}
			
			// Initialize PointInfo with owner to resolve pivot correctly
			offset.Init(m_Owner);
			
			// Get pivot node
			TNodeId pivot = offset.GetNodeId();
			
			// Add child with pivot if valid, otherwise just add as direct child
			if (pivot != -1)
			{
				m_Owner.AddChild(light, pivot, EAddChildFlags.AUTO_TRANSFORM);
			}
			else
			{
				m_Owner.AddChild(light, -1, EAddChildFlags.AUTO_TRANSFORM);
			}
			
			// Apply local transform from PointInfo
			vector xf[4];
			offset.GetLocalTransform(xf);
			light.SetLocalTransform(xf);
			
			light.SetEnabled(false);
			
			IEntity lightEntity = light;
			lightEntity.ClearFlags(EntityFlags.VISIBLE);
			
			m_mLights.Insert(src, light);
		}
		
		m_bLightsSpawned = true;
		m_bSpawnScheduled = false;
		
		ApplyLightState(m_bLightState);
	}

	protected void ParseStrobePattern()
	{
		if (!m_aPatternTimes)
			m_aPatternTimes = new array<float>();
		
		m_aPatternTimes.Clear();
		
		if (m_sStrobePattern.IsEmpty())
		{
			m_aPatternTimes.Insert(0.08);
			m_aPatternTimes.Insert(0.08);
			m_aPatternTimes.Insert(0.08);
			m_aPatternTimes.Insert(0.6);
			return;
		}
		
		array<string> tokens = {};
		m_sStrobePattern.Split(" ", tokens, false);
		
		if (tokens.IsEmpty())
		{
			m_aPatternTimes.Insert(0.08);
			m_aPatternTimes.Insert(0.08);
			m_aPatternTimes.Insert(0.08);
			m_aPatternTimes.Insert(0.6);
			return;
		}
		
		foreach (string token : tokens)
		{
			float time = token.ToFloat();
			if (time <= 0)
				time = 0.08;
			m_aPatternTimes.Insert(time);
		}
		
		if (m_aPatternTimes.IsEmpty())
		{
			m_aPatternTimes.Insert(0.08);
			m_aPatternTimes.Insert(0.08);
			m_aPatternTimes.Insert(0.08);
			m_aPatternTimes.Insert(0.6);
		}
	}

	protected void StrobeEffect()
	{
		if (!m_bStrobeEnabled)
			return;
		
		if (!m_aPatternTimes || m_aPatternTimes.IsEmpty())
			return;
		
		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().Remove(StrobeEffect);
		
		m_bLightState = !m_bLightState;
		ApplyLightState(m_bLightState);
		
		float delaySeconds = m_aPatternTimes.Get(m_iCurrentPatternIndex);
		int delayMs = delaySeconds * 1000;
		m_iCurrentPatternIndex = (m_iCurrentPatternIndex + 1) % m_aPatternTimes.Count();
		
		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().CallLater(StrobeEffect, delayMs, false);
	}

	override bool RplSave(ScriptBitWriter writer)
	{
		writer.WriteBool(m_bStrobeEnabled);
		writer.WriteBool(m_bIRCoverRemoved);
		writer.WriteInt(m_iCurrentColorIndex);
		writer.WriteFloat(m_fCurrentBrightness);
		return true;
	}

	override bool RplLoad(ScriptBitReader reader)
	{
		reader.ReadBool(m_bStrobeEnabled);
		reader.ReadBool(m_bIRCoverRemoved);
		reader.ReadInt(m_iCurrentColorIndex);
		reader.ReadFloat(m_fCurrentBrightness);
		
		// Always defer state application - ensure lights are spawned first
		// Use longer delay to ensure initialization is complete
		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().CallLater(ApplyStateFromReplication, 500, false);
		
		return true;
	}
	
	protected void ApplyStateFromReplication()
	{
		// Removed verbose logging - only log warnings/errors
		
		// Ensure lights are spawned before applying state
		if (!m_bLightsSpawned && m_aSources && !m_aSources.IsEmpty())
		{
			// Removed verbose logging - only log warnings/errors
			SpawnLights();
		}
		
		// Now apply the state
		if (m_bStrobeEnabled)
		{
			// Removed verbose logging - only log warnings/errors
			ApplyStrobeState(true);
		}
	}
	
	protected void ApplyStrobeStateDeferred(bool enabled)
	{
		ApplyStrobeState(enabled);
	}

	void ApplyStrobeState(bool enabled)
	{
		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().Remove(StrobeEffect);
		
		m_bStrobeEnabled = enabled;
		
		if (enabled)
		{
			if (!m_aPatternTimes || m_aPatternTimes.IsEmpty())
				ParseStrobePattern();
			
			m_iCurrentPatternIndex = 0;
			m_bLightState = false;
			
			if (!m_bLightsSpawned && m_aSources && !m_aSources.IsEmpty())
				SpawnLights();
			
			StrobeEffect();
		}
		else
		{
			m_bLightState = false;
			ApplyLightState(false);
		}
	}

	protected void ApplyLightState(bool state)
	{
		if (!m_mLights)
			return;
		
		vector currentColor = GetCurrentColor();
		
		for (int i = 0; i < m_mLights.Count(); i++)
		{
			ACM_NVL_LightSource src = m_mLights.GetKey(i);
			LightEntity light = m_mLights.GetElement(i);
			
			if (!src || !light)
				continue;
			
			bool isIR = src.IsIR();
			bool shouldBeVisible = state;
			
			// IR lights: visible when NVG active OR when cover is removed
			if (isIR && !m_bNVGActive && !m_bIRCoverRemoved)
				shouldBeVisible = false;
			
			IEntity lightEntity = light;
			if (shouldBeVisible)
			{
				light.SetEnabled(true);
				lightEntity.SetFlags(EntityFlags.VISIBLE);
				
				// Apply current color and brightness
				light.SetColor(Color.FromVector(currentColor), m_fCurrentBrightness);
			}
			else
			{
				lightEntity.ClearFlags(EntityFlags.VISIBLE);
				light.SetEnabled(false);
			}
			
			// Handle emissive material
			if (src.IsFlashlight() || (isIR && (m_bNVGActive || m_bIRCoverRemoved)))
			{
				if (m_EmissiveMat)
				{
					if (shouldBeVisible)
					{
						m_EmissiveMat.SetEmissiveMultiplier(m_fCurrentBrightness * 2);
						Color c = Color.FromVector(currentColor);
						m_EmissiveMat.SetEmissiveColor(c.PackToInt());
					}
					else
					{
						m_EmissiveMat.SetEmissiveMultiplier(0);
					}
				}
			}
			
			string soundEvent = src.GetSoundEvent();
			if (m_SoundComp && state && !soundEvent.IsEmpty())
				m_SoundComp.SoundEvent(soundEvent);
		}
	}
	
	void OnNVGStateChanged(bool nvgActive)
	{
		m_bNVGActive = nvgActive;
		
		if (m_bStrobeEnabled)
			ApplyLightState(m_bLightState);
	}

	protected IEntity GetCharacterOwner()
	{
		if (!m_Owner)
			return null;
		
		IEntity current = m_Owner;
		while (current)
		{
			if (ChimeraCharacter.Cast(current))
				return current;
			current = current.GetParent();
		}
		return null;
	}

	protected bool IsLocalPlayerOwner()
	{
		if (!Replication.IsRunning())
			return true;
		
		IEntity character = GetCharacterOwner();
		if (!character)
			return false;
		
		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return false;
		
		SCR_PlayerController scrPC = SCR_PlayerController.Cast(pc);
		if (!scrPC)
			return false;
		
		return scrPC.GetControlledEntity() == character;
	}

	protected void ScheduleActionListenerRetry(int delayMs)
	{
		if (m_iActionListenerRetryCount >= MAX_ACTION_LISTENER_RETRIES)
			return;

		m_iActionListenerRetryCount++;
		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().CallLater(RegisterActionListeners, delayMs, false);
	}

	protected void RegisterActionListeners()
	{
		if (m_bActionListenerRegistered)
			return;
		
		// Don't register on dedicated server
		if (Replication.IsRunning() && Replication.IsServer() && !GetGame().GetPlayerController())
			return;
		
		if (Replication.IsRunning())
		{
			IEntity character = GetCharacterOwner();
			PlayerController pc = GetGame().GetPlayerController();
			
			if (!pc)
			{
				ScheduleActionListenerRetry(200);
				return;
			}
			
			SCR_PlayerController scrPC = SCR_PlayerController.Cast(pc);
			IEntity controlled;
			if (scrPC)
				controlled = scrPC.GetControlledEntity();
			
			// Player/controller or owner not ready yet: capped retry.
			if (!controlled || !character)
			{
				ScheduleActionListenerRetry(200);
				return;
			}

			// Not the locally controlled character right now - often a transient spawn/possession
			// race, so retry (capped) instead of stopping permanently.
			if (controlled != character)
			{
				ScheduleActionListenerRetry(200);
				return;
			}
		}
		
		InputManager im = GetGame().GetInputManager();
		if (!im)
			return;
		
		if (!m_sToggleAction.IsEmpty())
			im.AddActionListener(m_sToggleAction, EActionTrigger.DOWN, OnStrobeKeyPressed);
		
		m_iActionListenerRetryCount = 0;
		m_bActionListenerRegistered = true;
	}

	protected void UnregisterActionListeners()
	{
		if (!m_bActionListenerRegistered)
			return;
		
		InputManager inputManager = GetGame().GetInputManager();
		if (!inputManager)
			return;
		
		if (!m_sToggleAction.IsEmpty())
			inputManager.RemoveActionListener(m_sToggleAction, EActionTrigger.DOWN, OnStrobeKeyPressed);
		
		m_bActionListenerRegistered = false;
	}

	protected void OnStrobeKeyPressed()
	{
		if (!IsLocalPlayerOwner())
			return;
		
		RequestToggleStrobe();
	}

	void RequestToggleStrobe()
	{
		ACM_NVL_NetworkComponent netComp = ACM_NVL_NetworkComponent.GetFromEntity(m_Owner);
		if (netComp)
			netComp.AskToggleStrobe(this);
		else
			ApplyStrobeState(!m_bStrobeEnabled);
	}

	void EnableStrobe()
	{
		if (m_bStrobeEnabled)
			return;
		
		ACM_NVL_NetworkComponent nc = ACM_NVL_NetworkComponent.GetFromEntity(m_Owner);
		if (nc)
			nc.AskEnableStrobe(this);
		else
			ApplyStrobeState(true);
	}

	void DisableStrobe()
	{
		if (!m_bStrobeEnabled)
			return;
		
		ACM_NVL_NetworkComponent nc = ACM_NVL_NetworkComponent.GetFromEntity(m_Owner);
		if (nc)
			nc.AskDisableStrobe(this);
		else
			ApplyStrobeState(false);
	}
	
	bool IsStrobeEnabled()
	{
		return m_bStrobeEnabled;
	}
	
	bool IsIRCoverRemoved()
	{
		return m_bIRCoverRemoved;
	}
	
	void SetIRCoverRemoved(bool removed)
	{
		m_bIRCoverRemoved = removed;
		
		// Update light state immediately if strobe is active
		if (m_bStrobeEnabled)
			ApplyLightState(m_bLightState);
		
		// Sync over network if we're the local owner and not suppressed
		if (!m_bSuppressNetworkSync && IsLocalPlayerOwner() && Replication.IsRunning())
		{
			ACM_NVL_NetworkComponent netComp = ACM_NVL_NetworkComponent.GetFromEntity(m_Owner);
			if (netComp)
				netComp.AskSetStrobeIRCoverRemoved(this, removed);
		}
	}
	
	void SetIRCoverRemovedFromNetwork(bool removed)
	{
		m_bSuppressNetworkSync = true;
		SetIRCoverRemoved(removed);
		m_bSuppressNetworkSync = false;
	}
	
	void ToggleIRCover()
	{
		SetIRCoverRemoved(!m_bIRCoverRemoved);
	}
	
	//------------------------------------------------------------------------------------------------
	// Color methods
	//------------------------------------------------------------------------------------------------
	int GetCurrentColorIndex() { return m_iCurrentColorIndex; }
	
	string GetCurrentColorName()
	{
		if (m_iCurrentColorIndex >= 0 && m_iCurrentColorIndex < s_aColorNames.Count())
			return s_aColorNames[m_iCurrentColorIndex];
		return "White";
	}
	
	vector GetCurrentColor()
	{
		if (m_iCurrentColorIndex >= 0 && m_iCurrentColorIndex < s_aLightColors.Count())
			return s_aLightColors[m_iCurrentColorIndex];
		return "1 1 1";
	}
	
	int GetColorCount()
	{
		return s_aLightColors.Count();
	}
	
	string GetColorNameByIndex(int index)
	{
		if (index >= 0 && index < s_aColorNames.Count())
			return s_aColorNames[index];
		return "White";
	}
	
	void SetStrobeColor(int colorIndex)
	{
		if (colorIndex < 0 || colorIndex >= s_aLightColors.Count())
			colorIndex = 0;
		
		m_iCurrentColorIndex = colorIndex;
		
		// Apply color if strobe is active
		if (m_bStrobeEnabled)
			ApplyLightState(m_bLightState);
		
		// Sync over network if we're the local owner and not suppressed
		if (!m_bSuppressNetworkSync && IsLocalPlayerOwner() && Replication.IsRunning())
		{
			ACM_NVL_NetworkComponent netComp = ACM_NVL_NetworkComponent.GetFromEntity(m_Owner);
			if (netComp)
				netComp.AskSetStrobeColor(this, colorIndex);
		}
	}
	
	void SetStrobeColorFromNetwork(int colorIndex)
	{
		m_bSuppressNetworkSync = true;
		SetStrobeColor(colorIndex);
		m_bSuppressNetworkSync = false;
	}
	
	void CycleStrobeColor()
	{
		int newIndex = (m_iCurrentColorIndex + 1) % s_aLightColors.Count();
		SetStrobeColor(newIndex);
	}
	
	//------------------------------------------------------------------------------------------------
	// Brightness methods
	//------------------------------------------------------------------------------------------------
	float GetBrightness() { return m_fCurrentBrightness; }
	float GetMinBrightness() { return m_fMinBrightness; }
	float GetMaxBrightness() { return m_fMaxBrightness; }
	
	void SetBrightness(float brightness)
	{
		m_fCurrentBrightness = Math.Clamp(brightness, m_fMinBrightness, m_fMaxBrightness);
		
		// Apply brightness if strobe is active
		if (m_bStrobeEnabled)
			ApplyLightState(m_bLightState);
		
		// Sync over network if we're the local owner and not suppressed
		if (!m_bSuppressNetworkSync && IsLocalPlayerOwner() && Replication.IsRunning())
		{
			ACM_NVL_NetworkComponent netComp = ACM_NVL_NetworkComponent.GetFromEntity(m_Owner);
			if (netComp)
				netComp.AskSetStrobeBrightness(this, m_fCurrentBrightness);
		}
	}
	
	void SetBrightnessFromNetwork(float brightness)
	{
		m_bSuppressNetworkSync = true;
		SetBrightness(brightness);
		m_bSuppressNetworkSync = false;
	}
	
	void AdjustBrightness(bool increase)
	{
		float step = m_fBrightnessStep;
		
		// Finer control at low brightness
		if (m_fCurrentBrightness < 1.0)
			step = Math.Min(step, 0.05);
		else if (m_fCurrentBrightness < 2.0)
			step = Math.Min(step, 0.1);
		
		if (!increase)
			step = -step;
		
		SetBrightness(m_fCurrentBrightness + step);
	}
	
	// Alias for radial menu compatibility
	bool IsStrobeActive()
	{
		return m_bStrobeEnabled;
	}
	
	// Alias for radial menu compatibility
	void RequestToggle()
	{
		RequestToggleStrobe();
	}
	
	// Cycle through strobe modes (placeholder - extend for multiple patterns)
	void CycleMode()
	{
		// For now just toggle, can be extended for multiple strobe patterns
		RequestToggleStrobe();
	}
	
	// Get current mode name for UI
	string GetCurrentModeName()
	{
		if (m_bStrobeEnabled)
			return "Strobe";
		return "Off";
	}

	override void OnDelete(IEntity owner)
	{
		if (GetGame() && GetGame().GetCallqueue())
		{
			GetGame().GetCallqueue().Remove(StrobeEffect);
			GetGame().GetCallqueue().Remove(DeferredInit);
			GetGame().GetCallqueue().Remove(ApplyStrobeStateDeferred);
			GetGame().GetCallqueue().Remove(ApplyStateFromReplication);
			GetGame().GetCallqueue().Remove(RegisterActionListeners);
			GetGame().GetCallqueue().Remove(RegisterWithIRSystem);
		}
		
		UnregisterFromIRSystem();
		
		if (m_bActionListenerRegistered)
			UnregisterActionListeners();
		
		if (m_bStrobeEnabled)
		{
			m_bStrobeEnabled = false;
			ApplyLightState(false);
		}
		
		if (m_mLights)
		{
			for (int i = 0; i < m_mLights.Count(); i++)
			{
				LightEntity light = m_mLights.GetElement(i);
				if (light)
					SCR_EntityHelper.DeleteEntityAndChildren(light);
			}
			m_mLights.Clear();
		}
		
		m_mLights = null;
	}
}
