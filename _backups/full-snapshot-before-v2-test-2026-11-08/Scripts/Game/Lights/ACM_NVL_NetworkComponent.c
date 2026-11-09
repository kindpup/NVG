//------------------------------------------------------------------------------------------------
// ACM_NVL_NetworkComponent.c
// Network relay for equipment lights and NVG/Thermal - lives on CHARACTER prefab
// Part of ACM Night Vision and Lights mod
//------------------------------------------------------------------------------------------------

[ComponentEditorProps(category: "ACM/Network", description: "Network relay for equipment lights and NVG/Thermal. Add to character prefab.")]
class ACM_NVL_NetworkComponentClass : ScriptComponentClass {}

class ACM_NVL_NetworkComponent : ScriptComponent
{
	protected static const int MAX_LIGHT_SEARCH_DEPTH = 20;

	// Tracked states for JIP synchronization - now per-component using string Light Identifier
	protected ref map<string, bool> m_mFlashlightStates = new map<string, bool>();
	protected ref map<string, bool> m_mIRStates = new map<string, bool>();
	protected ref map<string, int> m_mLightColorIndices = new map<string, int>();
	protected ref map<string, bool> m_mIRFilterRemoved = new map<string, bool>();
	protected ref map<string, float> m_mFlashlightBrightness = new map<string, float>();
	protected ref map<string, float> m_mIRBrightness = new map<string, float>();
	protected ref map<string, int> m_mBeamAngles = new map<string, int>();
	
	// Strobe states
	protected bool m_bStrobeState;
	protected int m_iStrobeColorIndex;
	protected bool m_bStrobeIRCoverRemoved;
	protected float m_fStrobeBrightness = 3.0;
	
	// Guardian Angel states - per-component using entity RplId as int
	protected ref map<int, bool> m_mGuardianAngelStates = new map<int, bool>();
	protected ref map<int, int> m_mGuardianAngelModes = new map<int, int>();
	protected ref map<int, int> m_mGuardianAngelPatterns = new map<int, int>();
	protected ref map<int, float> m_mGuardianAngelBrightness = new map<int, float>();

	// Direct registration map: identifier → component.
	// Populated by LightsComponents that self-register during DeferredInit.
	// Provides O(1) lookup in RPCs instead of relying on GetWeaponsList hierarchy search.
	protected ref map<string, ACM_NVL_LightsComponent> m_mRegisteredLights = new map<string, ACM_NVL_LightsComponent>();
	// Reverse map: component → identifier, used to detect and fix ID changes on re-registration.
	protected ref map<ACM_NVL_LightsComponent, string> m_mRegisteredLightsReverse = new map<ACM_NVL_LightsComponent, string>();

	void RegisterLightsComponent(ACM_NVL_LightsComponent comp, string id)
	{
		if (!comp || id.IsEmpty()) return;

		// If the component was previously registered under a different ID (e.g. replication
		// settled after a retry and the identifier changed), remove the stale forward entry.
		string oldId = m_mRegisteredLightsReverse.Get(comp);
		if (!oldId.IsEmpty() && oldId != id)
			m_mRegisteredLights.Remove(oldId);

		m_mRegisteredLights.Set(id, comp);
		m_mRegisteredLightsReverse.Set(comp, id);

		// Immediately apply any state that arrived via RPC before this component registered.
		ApplyStoredStateToComponent(comp, id);
	}

	void UnregisterLightsComponent(string id)
	{
		if (id.IsEmpty()) return;
		ACM_NVL_LightsComponent comp = m_mRegisteredLights.Get(id);
		if (comp)
			m_mRegisteredLightsReverse.Remove(comp);
		m_mRegisteredLights.Remove(id);
	}
	
	//------------------------------------------------------------------------------------------------
	// Helper to get Light Identifier from a LightsComponent
	//------------------------------------------------------------------------------------------------
	protected string GetLightIdentifier(ACM_NVL_LightsComponent comp)
	{
		if (!comp)
			return "";
		// Prefer the stable id cached at registration time (m_sRegisteredNetworkId).
		// This avoids re-walking the RplId ancestor chain on every RPC call and guarantees
		// that the identifier sent in RPCs is exactly the key stored in m_mRegisteredLights,
		// even in DS edge cases where replication is still settling.
		// Falls back to GetResolvedNetworkIdentifier() before registration completes.
		return comp.GetRegisteredIdentifier();
	}
	
	protected ACM_NVL_LightsComponent FindLightsComponentByIdentifier(string identifier)
	{
		if (identifier.IsEmpty())
			return null;

		// Fast path: direct registry lookup
		ACM_NVL_LightsComponent registered = m_mRegisteredLights.Get(identifier);
		if (registered)
		{
			// Validate the cached ref: ensure the component and its owner are still alive
			// and that the component's stable registered identifier still matches the key.
			IEntity regOwner = registered.GetOwner();
			if (regOwner && registered.GetRegisteredIdentifier() == identifier)
				return registered;

			// Stale ref \u2013 remove and fall through to slow search
			m_mRegisteredLightsReverse.Remove(registered);
			m_mRegisteredLights.Remove(identifier);
		}

		// Slow path: exhaustive hierarchy search (fallback before registration completes)
		array<ACM_NVL_LightsComponent> allLights = {};
		FindAllLightsOnCharacter(allLights);

		foreach (ACM_NVL_LightsComponent lights : allLights)
		{
			string thisId = GetLightIdentifier(lights);
			if (thisId == identifier)
			{
				// Cache the result (includes reverse entry)
				m_mRegisteredLights.Set(identifier, lights);
				m_mRegisteredLightsReverse.Set(lights, identifier);
				return lights;
			}
		}

		return null;
	}
	
	protected ACM_NVL_GuardianAngelComponent FindGuardianAngelByRplIdInt(int idInt)
	{
		if (idInt < 0) return null;
		
		array<ACM_NVL_GuardianAngelComponent> guardians = {};
		FindAllGuardianAngelsOnCharacter(guardians);
		
		foreach (ACM_NVL_GuardianAngelComponent guardian : guardians)
		{
			if (GetComponentRplIdInt(guardian) == idInt)
				return guardian;
		}
		return null;
	}
	
	// Helper for GuardianAngel components (still uses RplId)
	protected int GetComponentRplIdInt(ScriptComponent comp)
	{
		if (!comp) return -1;
		IEntity owner = comp.GetOwner();
		if (!owner) return -1;
		return Replication.FindId(owner);
	}
	
	//------------------------------------------------------------------------------------------------
	// RplSave/RplLoad for Join In Progress
	//------------------------------------------------------------------------------------------------
	override bool RplSave(ScriptBitWriter writer)
	{
		// Save per-component light states FROM STORED MAPS (authoritative on server)
		// Note: We write the map contents directly, not from components, because on dedicated server
		// the light entities may not exist (they're spawned locally via SpawnEntityPrefabLocal)
		
		// Collect all unique component identifiers from all light state maps
		ref set<string> uniqueIds = new set<string>();
		for (int i = 0; i < m_mFlashlightStates.Count(); i++)
			uniqueIds.Insert(m_mFlashlightStates.GetKey(i));
		for (int i = 0; i < m_mIRStates.Count(); i++)
			uniqueIds.Insert(m_mIRStates.GetKey(i));
		for (int i = 0; i < m_mLightColorIndices.Count(); i++)
			uniqueIds.Insert(m_mLightColorIndices.GetKey(i));
		for (int i = 0; i < m_mIRFilterRemoved.Count(); i++)
			uniqueIds.Insert(m_mIRFilterRemoved.GetKey(i));
		for (int i = 0; i < m_mFlashlightBrightness.Count(); i++)
			uniqueIds.Insert(m_mFlashlightBrightness.GetKey(i));
		for (int i = 0; i < m_mIRBrightness.Count(); i++)
			uniqueIds.Insert(m_mIRBrightness.GetKey(i));
		for (int i = 0; i < m_mBeamAngles.Count(); i++)
			uniqueIds.Insert(m_mBeamAngles.GetKey(i));
		
		// Write count and state for each tracked component
		array<string> idArray = {};
		for (int i = 0; i < uniqueIds.Count(); i++)
			idArray.Insert(uniqueIds.Get(i));
		
		writer.WriteInt(idArray.Count());
		// Removed verbose logging - only log warnings/errors
		
		foreach (string id : idArray)
		{
			bool flashOn = false;
			bool irOn = false;
			int colorIndex = 0;
			bool filterRemoved = false;
			float flashBrightness = 6.0;
			float irBrightness = 1.0;
			int beamAngle = 25;
			
			if (m_mFlashlightStates.Contains(id))
				flashOn = m_mFlashlightStates.Get(id);
			if (m_mIRStates.Contains(id))
				irOn = m_mIRStates.Get(id);
			if (m_mLightColorIndices.Contains(id))
				colorIndex = m_mLightColorIndices.Get(id);
			if (m_mIRFilterRemoved.Contains(id))
				filterRemoved = m_mIRFilterRemoved.Get(id);
			if (m_mFlashlightBrightness.Contains(id))
				flashBrightness = m_mFlashlightBrightness.Get(id);
			if (m_mIRBrightness.Contains(id))
				irBrightness = m_mIRBrightness.Get(id);
			if (m_mBeamAngles.Contains(id))
				beamAngle = m_mBeamAngles.Get(id);
			
			writer.WriteString(id);
			writer.WriteBool(flashOn);
			writer.WriteBool(irOn);
			writer.WriteInt(colorIndex);
			writer.WriteBool(filterRemoved);
			writer.WriteFloat(flashBrightness);
			writer.WriteFloat(irBrightness);
			writer.WriteInt(beamAngle);
			
			// Removed verbose logging - only log warnings/errors
		}
		
		// Save strobe states
		writer.WriteBool(m_bStrobeState);
		writer.WriteInt(m_iStrobeColorIndex);
		writer.WriteBool(m_bStrobeIRCoverRemoved);
		writer.WriteFloat(m_fStrobeBrightness);
		
		// FPS patch: Guardian Angel disabled; preserve stream format with zero Guardian entries.
		writer.WriteInt(0);
		
		return true;
	}
	
	override bool RplLoad(ScriptBitReader reader)
	{
		// Removed verbose logging - only log warnings/errors
		
		// Load per-component light states
		int lightCount;
		reader.ReadInt(lightCount);
		
		// Removed verbose logging - only log warnings/errors
		
		for (int i = 0; i < lightCount; i++)
		{
			string id;
			bool flashOn, irOn, filterRemoved;
			int colorIndex;
			float flashBrightness, irBrightness;
			int beamAngle;
			
			reader.ReadString(id);
			reader.ReadBool(flashOn);
			reader.ReadBool(irOn);
			reader.ReadInt(colorIndex);
			reader.ReadBool(filterRemoved);
			reader.ReadFloat(flashBrightness);
			reader.ReadFloat(irBrightness);
			reader.ReadInt(beamAngle);
			
			// Removed verbose logging - only log warnings/errors
			
			// Store in maps for deferred application
			m_mFlashlightStates.Set(id, flashOn);
			m_mIRStates.Set(id, irOn);
			m_mLightColorIndices.Set(id, colorIndex);
			m_mIRFilterRemoved.Set(id, filterRemoved);
			m_mFlashlightBrightness.Set(id, flashBrightness);
			m_mIRBrightness.Set(id, irBrightness);
			m_mBeamAngles.Set(id, beamAngle);
		}
		
		// Load strobe states
		reader.ReadBool(m_bStrobeState);
		reader.ReadInt(m_iStrobeColorIndex);
		reader.ReadBool(m_bStrobeIRCoverRemoved);
		reader.ReadFloat(m_fStrobeBrightness);
		
		// FPS patch: Guardian Angel disabled. Read and discard any legacy entries to preserve stream format.
		int guardianCount;
		reader.ReadInt(guardianCount);
		for (int j = 0; j < guardianCount; j++)
		{
			int id;
			bool enabled;
			int mode, pattern;
			float brightness;
			reader.ReadInt(id);
			reader.ReadBool(enabled);
			reader.ReadInt(mode);
			reader.ReadInt(pattern);
			reader.ReadFloat(brightness);
		}
		m_mGuardianAngelStates.Clear();
		m_mGuardianAngelModes.Clear();
		m_mGuardianAngelPatterns.Clear();
		m_mGuardianAngelBrightness.Clear();
		
		// Defer applying states to allow components to initialize
		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().CallLater(ApplyAllStatesFromJIP, 750, false);
		
		return true;
	}
	
	protected int m_iJIPRetryCount = 0;
	protected const int MAX_JIP_RETRIES = 5;
	
	protected void ApplyAllStatesFromJIP()
	{
		m_iJIPRetryCount++;
		// Removed verbose logging - only log warnings/errors
		
		bool anyStatePending = false;
		
		// Apply light states per-component using string identifiers
		array<ACM_NVL_LightsComponent> allLights = {};
		FindAllLightsOnCharacter(allLights);
		
		// Removed verbose logging - only log warnings/errors
		
		// Track which IDs were found and applied
		ref set<string> appliedLightIds = new set<string>();
		
		foreach (ACM_NVL_LightsComponent lights : allLights)
		{
			if (!lights) continue;
			
			string id = GetLightIdentifier(lights);
			if (id.IsEmpty()) continue;
			
			appliedLightIds.Insert(id);
			
			if (m_mLightColorIndices.Contains(id))
				lights.SetLightColorFromNetwork(m_mLightColorIndices.Get(id));
			
			if (m_mFlashlightBrightness.Contains(id))
				lights.SetFlashlightBrightnessFromNetwork(m_mFlashlightBrightness.Get(id));
			
			if (m_mIRBrightness.Contains(id))
				lights.SetIRBrightnessFromNetwork(m_mIRBrightness.Get(id));
			
			if (m_mBeamAngles.Contains(id))
				lights.SetConeAngleFromNetwork(m_mBeamAngles.Get(id));
			
			if (m_mIRFilterRemoved.Contains(id))
				lights.SetIRFilterRemovedFromNetwork(m_mIRFilterRemoved.Get(id));
			
			if (m_mFlashlightStates.Contains(id))
				lights.ApplyFlashlightState(m_mFlashlightStates.Get(id));

			if (m_mIRStates.Contains(id))
				lights.ApplyIRState(m_mIRStates.Get(id));
		}
		
		// Check if there are pending states that weren't applied
		for (int i = 0; i < m_mFlashlightStates.Count(); i++)
		{
			string pendingId = m_mFlashlightStates.GetKey(i);
			bool pendingState = m_mFlashlightStates.GetElement(i);
			if (pendingState && !appliedLightIds.Contains(pendingId))
			{
				anyStatePending = true;
			}
		}
		
		for (int i = 0; i < m_mIRStates.Count(); i++)
		{
			string pendingId = m_mIRStates.GetKey(i);
			bool pendingState = m_mIRStates.GetElement(i);
			if (pendingState && !appliedLightIds.Contains(pendingId))
			{
				anyStatePending = true;
			}
		}
		
		// Apply strobe states
		array<ACM_NVL_StrobeComponent> strobes = {};
		FindAllStrobesOnCharacter(strobes);
		
		foreach (ACM_NVL_StrobeComponent strobe : strobes)
		{
			if (!strobe) continue;
			
			strobe.SetStrobeColorFromNetwork(m_iStrobeColorIndex);
			strobe.SetBrightnessFromNetwork(m_fStrobeBrightness);
			strobe.SetIRCoverRemovedFromNetwork(m_bStrobeIRCoverRemoved);
			
			if (m_bStrobeState)
				strobe.ApplyStrobeState(true);
		}
		
		// FPS patch: Guardian Angel disabled; skip Guardian component scans.
		
		// Check strobe state pending
		if (m_bStrobeState && strobes.Count() == 0)
			anyStatePending = true;
		
		// Retry if there are pending states and we haven't exceeded max retries
		if (anyStatePending && m_iJIPRetryCount < MAX_JIP_RETRIES)
		{
			if (GetGame() && GetGame().GetCallqueue())
				GetGame().GetCallqueue().CallLater(ApplyAllStatesFromJIP, 500, false);
		}
		else if (!anyStatePending)
		{
			m_iJIPRetryCount = 0; // Reset for future JIP events
		}
	}

	//------------------------------------------------------------------------------------------------
	// Strobe Controls
	//------------------------------------------------------------------------------------------------
	void AskToggleStrobe(ACM_NVL_StrobeComponent strobe)
	{
		if (!strobe) return;
		bool newState = !strobe.IsStrobeEnabled();
		strobe.ApplyStrobeState(newState);
		if (Replication.IsRunning()) Rpc(Rpc_AskToggleStrobe, newState);
	}

	void AskEnableStrobe(ACM_NVL_StrobeComponent strobe)
	{
		if (!strobe || strobe.IsStrobeEnabled()) return;
		strobe.ApplyStrobeState(true);
		if (Replication.IsRunning()) Rpc(Rpc_AskToggleStrobe, true);
	}

	void AskDisableStrobe(ACM_NVL_StrobeComponent strobe)
	{
		if (!strobe || !strobe.IsStrobeEnabled()) return;
		strobe.ApplyStrobeState(false);
		if (Replication.IsRunning()) Rpc(Rpc_AskToggleStrobe, false);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void Rpc_AskToggleStrobe(bool enabled)
	{
		m_bStrobeState = enabled;
		
		array<ACM_NVL_StrobeComponent> strobes = {};
		FindAllStrobesOnCharacter(strobes);
		foreach (ACM_NVL_StrobeComponent strobe : strobes)
		{
			if (strobe) strobe.ApplyStrobeState(enabled);
		}
		Rpc(Rpc_DoToggleStrobe, enabled);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast, RplCondition.NoOwner)]
	protected void Rpc_DoToggleStrobe(bool enabled)
	{
		array<ACM_NVL_StrobeComponent> strobes = {};
		FindAllStrobesOnCharacter(strobes);
		foreach (ACM_NVL_StrobeComponent strobe : strobes)
		{
			if (strobe) strobe.ApplyStrobeState(enabled);
		}
	}
	
	void AskSetStrobeColor(ACM_NVL_StrobeComponent strobe, int colorIndex)
	{
		if (!strobe) return;
		strobe.SetStrobeColor(colorIndex);
		if (Replication.IsRunning()) Rpc(Rpc_AskSetStrobeColor, colorIndex);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void Rpc_AskSetStrobeColor(int colorIndex)
	{
		m_iStrobeColorIndex = colorIndex;
		
		array<ACM_NVL_StrobeComponent> strobes = {};
		FindAllStrobesOnCharacter(strobes);
		foreach (ACM_NVL_StrobeComponent strobe : strobes)
		{
			if (strobe) strobe.SetStrobeColorFromNetwork(colorIndex);
		}
		Rpc(Rpc_DoSetStrobeColor, colorIndex);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast, RplCondition.NoOwner)]
	protected void Rpc_DoSetStrobeColor(int colorIndex)
	{
		array<ACM_NVL_StrobeComponent> strobes = {};
		FindAllStrobesOnCharacter(strobes);
		foreach (ACM_NVL_StrobeComponent strobe : strobes)
		{
			if (strobe) strobe.SetStrobeColorFromNetwork(colorIndex);
		}
	}
	
	void AskSetStrobeBrightness(ACM_NVL_StrobeComponent strobe, float brightness)
	{
		if (!strobe) return;
		strobe.SetBrightness(brightness);
		if (Replication.IsRunning()) Rpc(Rpc_AskSetStrobeBrightness, brightness);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void Rpc_AskSetStrobeBrightness(float brightness)
	{
		m_fStrobeBrightness = brightness;
		
		array<ACM_NVL_StrobeComponent> strobes = {};
		FindAllStrobesOnCharacter(strobes);
		foreach (ACM_NVL_StrobeComponent strobe : strobes)
		{
			if (strobe) strobe.SetBrightnessFromNetwork(brightness);
		}
		Rpc(Rpc_DoSetStrobeBrightness, brightness);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast, RplCondition.NoOwner)]
	protected void Rpc_DoSetStrobeBrightness(float brightness)
	{
		array<ACM_NVL_StrobeComponent> strobes = {};
		FindAllStrobesOnCharacter(strobes);
		foreach (ACM_NVL_StrobeComponent strobe : strobes)
		{
			if (strobe) strobe.SetBrightnessFromNetwork(brightness);
		}
	}
	
	void AskSetStrobeIRCoverRemoved(ACM_NVL_StrobeComponent strobe, bool removed)
	{
		if (!strobe) return;
		strobe.SetIRCoverRemoved(removed);
		if (Replication.IsRunning()) Rpc(Rpc_AskSetStrobeIRCoverRemoved, removed);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void Rpc_AskSetStrobeIRCoverRemoved(bool removed)
	{
		m_bStrobeIRCoverRemoved = removed;
		
		array<ACM_NVL_StrobeComponent> strobes = {};
		FindAllStrobesOnCharacter(strobes);
		foreach (ACM_NVL_StrobeComponent strobe : strobes)
		{
			if (strobe) strobe.SetIRCoverRemovedFromNetwork(removed);
		}
		Rpc(Rpc_DoSetStrobeIRCoverRemoved, removed);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast, RplCondition.NoOwner)]
	protected void Rpc_DoSetStrobeIRCoverRemoved(bool removed)
	{
		array<ACM_NVL_StrobeComponent> strobes = {};
		FindAllStrobesOnCharacter(strobes);
		foreach (ACM_NVL_StrobeComponent strobe : strobes)
		{
			if (strobe) strobe.SetIRCoverRemovedFromNetwork(removed);
		}
	}

	protected void FindAllStrobesOnCharacter(out array<ACM_NVL_StrobeComponent> strobes)
	{
		IEntity owner = GetOwner();
		if (!owner) return;
		
		FindStrobesRecursive(owner, strobes);
		
		ChimeraCharacter char = ChimeraCharacter.Cast(owner);
		if (char)
		{
			BaseWeaponManagerComponent weaponMgr = BaseWeaponManagerComponent.Cast(char.FindComponent(BaseWeaponManagerComponent));
			if (weaponMgr)
			{
				array<IEntity> weapons = {};
				weaponMgr.GetWeaponsList(weapons);
				foreach (IEntity weapon : weapons)
				{
					if (weapon)
						FindStrobesRecursive(weapon, strobes);
				}
			}
		}
	}
	
	protected void FindStrobesRecursive(IEntity entity, out array<ACM_NVL_StrobeComponent> outArray)
	{
		if (!entity) return;
		
		ACM_NVL_StrobeComponent comp = ACM_NVL_StrobeComponent.Cast(entity.FindComponent(ACM_NVL_StrobeComponent));
		if (comp && outArray.Find(comp) == -1)
			outArray.Insert(comp);
		
		IEntity child = entity.GetChildren();
		while (child)
		{
			FindStrobesRecursive(child, outArray);
			child = child.GetSibling();
		}
	}

	//------------------------------------------------------------------------------------------------
	// Guardian Angel Controls - disabled by FPS patch.
	//------------------------------------------------------------------------------------------------
	void AskToggleGuardianAngel(ACM_NVL_GuardianAngelComponent guardian) {}
	void AskSetGuardianAngelEnabled(ACM_NVL_GuardianAngelComponent guardian, bool enabled) {}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void Rpc_AskSetGuardianAngelEnabled(int componentId, bool enabled) {}

	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast, RplCondition.NoOwner)]
	protected void Rpc_DoSetGuardianAngelEnabled(int componentId, bool enabled) {}
	
	void AskSetGuardianAngelMode(ACM_NVL_GuardianAngelComponent guardian, int mode) {}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void Rpc_AskSetGuardianAngelMode(int componentId, int mode) {}

	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast, RplCondition.NoOwner)]
	protected void Rpc_DoSetGuardianAngelMode(int componentId, int mode) {}
	
	void AskSetGuardianAngelPattern(ACM_NVL_GuardianAngelComponent guardian, int pattern) {}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void Rpc_AskSetGuardianAngelPattern(int componentId, int pattern) {}

	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast, RplCondition.NoOwner)]
	protected void Rpc_DoSetGuardianAngelPattern(int componentId, int pattern) {}
	
	void AskSetGuardianAngelBrightness(ACM_NVL_GuardianAngelComponent guardian, float brightness) {}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void Rpc_AskSetGuardianAngelBrightness(int componentId, float brightness) {}

	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast, RplCondition.NoOwner)]
	protected void Rpc_DoSetGuardianAngelBrightness(int componentId, float brightness) {}

	protected void FindAllGuardianAngelsOnCharacter(out array<ACM_NVL_GuardianAngelComponent> guardians)
	{
		// FPS patch: Guardian Angel disabled.
	}
	
	protected void FindGuardianAngelsRecursive(IEntity entity, out array<ACM_NVL_GuardianAngelComponent> outArray)
	{
		// FPS patch: Guardian Angel disabled.
	}

	//------------------------------------------------------------------------------------------------
	// Flashlight Controls - PER-COMPONENT using string Light Identifier
	//------------------------------------------------------------------------------------------------
	void AskSetFlashlight(ACM_NVL_LightsComponent lights, bool state)
	{
		if (!lights) return;
		lights.ApplyFlashlightState(state);
		
		string id = GetLightIdentifier(lights);
		// Removed verbose logging - only log warnings/errors
		
		// Store state for JIP
		if (!id.IsEmpty())
			m_mFlashlightStates.Set(id, state);
		
		if (Replication.IsRunning() && !id.IsEmpty()) 
			Rpc(Rpc_AskSetFlashlight, id, state);
	}

	void AskToggleFlashlight(ACM_NVL_LightsComponent lights)
	{
		if (!lights) return;
		bool newState = !lights.IsFlashlightOn();
		lights.ApplyFlashlightState(newState);

		string id = GetLightIdentifier(lights);
		if (!id.IsEmpty())
			m_mFlashlightStates.Set(id, newState);

		if (!Replication.IsRunning())
		{
			return;
		}
		if (id.IsEmpty())
		{
			return;
		}
		Rpc(Rpc_AskSetFlashlight, id, newState);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void Rpc_AskSetFlashlight(string identifier, bool enabled)
	{
		m_mFlashlightStates.Set(identifier, enabled);

		ACM_NVL_LightsComponent lights = FindLightsComponentByIdentifier(identifier);
		if (lights)
		{
			lights.ApplyFlashlightState(enabled);
		}
		else
		{
			SchedulePendingApply(identifier);
		}
		
		Rpc(Rpc_DoSetFlashlight, identifier, enabled);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast, RplCondition.NoOwner)]
	protected void Rpc_DoSetFlashlight(string identifier, bool enabled)
	{
		ACM_NVL_LightsComponent lights = FindLightsComponentByIdentifier(identifier);
		if (lights)
		{
			lights.ApplyFlashlightState(enabled);
		}
		else
		{
			SchedulePendingApply(identifier);
		}
	}

	//------------------------------------------------------------------------------------------------
	// IR Controls - PER-COMPONENT using string Light Identifier
	//------------------------------------------------------------------------------------------------
	void AskSetIR(ACM_NVL_LightsComponent lights, bool state)
	{
		if (!lights) return;
		lights.ApplyIRState(state);
		
		string id = GetLightIdentifier(lights);
		// Store state for JIP
		if (!id.IsEmpty())
			m_mIRStates.Set(id, state);
		
		if (Replication.IsRunning() && !id.IsEmpty()) 
			Rpc(Rpc_AskSetIR, id, state);
	}

	void AskToggleIR(ACM_NVL_LightsComponent lights)
	{
		if (!lights) return;
		bool newState = !lights.IsIROn();
		lights.ApplyIRState(newState);

		string id = GetLightIdentifier(lights);
		if (!id.IsEmpty())
			m_mIRStates.Set(id, newState);

		if (!Replication.IsRunning())
		{
			return;
		}
		if (id.IsEmpty())
		{
			return;
		}
		Rpc(Rpc_AskSetIR, id, newState);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void Rpc_AskSetIR(string identifier, bool enabled)
	{
		m_mIRStates.Set(identifier, enabled);
		
		ACM_NVL_LightsComponent lights = FindLightsComponentByIdentifier(identifier);
		if (lights)
			lights.ApplyIRState(enabled);
		else
			SchedulePendingApply(identifier);
		
		Rpc(Rpc_DoSetIR, identifier, enabled);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast, RplCondition.NoOwner)]
	protected void Rpc_DoSetIR(string identifier, bool enabled)
	{
		ACM_NVL_LightsComponent lights = FindLightsComponentByIdentifier(identifier);
		if (lights)
			lights.ApplyIRState(enabled);
		else
			SchedulePendingApply(identifier);
	}
	
	//------------------------------------------------------------------------------------------------
	// Light Color Controls - PER-COMPONENT using string Light Identifier
	//------------------------------------------------------------------------------------------------
	void AskSetLightColor(ACM_NVL_LightsComponent lights, int colorIndex)
	{
		if (!lights) return;
		lights.SetLightColor(colorIndex);
		
		string id = GetLightIdentifier(lights);
		if (!id.IsEmpty())
			m_mLightColorIndices.Set(id, colorIndex);
		
		if (Replication.IsRunning() && !id.IsEmpty()) 
			Rpc(Rpc_AskSetLightColor, id, colorIndex);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void Rpc_AskSetLightColor(string identifier, int colorIndex)
	{
		m_mLightColorIndices.Set(identifier, colorIndex);
		
		ACM_NVL_LightsComponent lights = FindLightsComponentByIdentifier(identifier);
		if (lights)
			lights.SetLightColorFromNetwork(colorIndex);
		
		Rpc(Rpc_DoSetLightColor, identifier, colorIndex);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast, RplCondition.NoOwner)]
	protected void Rpc_DoSetLightColor(string identifier, int colorIndex)
	{
		ACM_NVL_LightsComponent lights = FindLightsComponentByIdentifier(identifier);
		if (lights)
			lights.SetLightColorFromNetwork(colorIndex);
	}
	
	//------------------------------------------------------------------------------------------------
	// Light Brightness Controls - PER-COMPONENT using string Light Identifier
	//------------------------------------------------------------------------------------------------
	void AskSetFlashlightBrightness(ACM_NVL_LightsComponent lights, float brightness)
	{
		if (!lights) return;
		// NOTE: no local apply here – AdjustBrightness (the only caller) already applied locally.
		string id = GetLightIdentifier(lights);
		if (!id.IsEmpty())
			m_mFlashlightBrightness.Set(id, brightness);

		if (!Replication.IsRunning())
		{
			return;
		}
		if (id.IsEmpty())
		{
			return;
		}
		Rpc(Rpc_AskSetFlashlightBrightness, id, brightness);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void Rpc_AskSetFlashlightBrightness(string identifier, float brightness)
	{
		m_mFlashlightBrightness.Set(identifier, brightness);

		ACM_NVL_LightsComponent lights = FindLightsComponentByIdentifier(identifier);
		if (lights)
			lights.SetFlashlightBrightnessFromNetwork(brightness);
		
		Rpc(Rpc_DoSetFlashlightBrightness, identifier, brightness);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast, RplCondition.NoOwner)]
	protected void Rpc_DoSetFlashlightBrightness(string identifier, float brightness)
	{
		ACM_NVL_LightsComponent lights = FindLightsComponentByIdentifier(identifier);
		if (lights)
			lights.SetFlashlightBrightnessFromNetwork(brightness);
	}
	
	void AskSetIRBrightness(ACM_NVL_LightsComponent lights, float brightness)
	{
		if (!lights) return;
		// NOTE: no local apply here – AdjustBrightness (the only caller) already applied locally.
		string id = GetLightIdentifier(lights);
		if (!id.IsEmpty())
			m_mIRBrightness.Set(id, brightness);

		if (!Replication.IsRunning())
		{
			return;
		}
		if (id.IsEmpty())
		{
			return;
		}
		Rpc(Rpc_AskSetIRBrightness, id, brightness);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void Rpc_AskSetIRBrightness(string identifier, float brightness)
	{
		m_mIRBrightness.Set(identifier, brightness);
		
		ACM_NVL_LightsComponent lights = FindLightsComponentByIdentifier(identifier);
		if (lights)
			lights.SetIRBrightnessFromNetwork(brightness);
		
		Rpc(Rpc_DoSetIRBrightness, identifier, brightness);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast, RplCondition.NoOwner)]
	protected void Rpc_DoSetIRBrightness(string identifier, float brightness)
	{
		ACM_NVL_LightsComponent lights = FindLightsComponentByIdentifier(identifier);
		if (lights)
			lights.SetIRBrightnessFromNetwork(brightness);
	}
	
	void AskSetBeamAngle(ACM_NVL_LightsComponent lights, int angle)
	{
		if (!lights)
		{
			return;
		}

		// NOTE: no local apply here – AdjustConeAngle (the only caller) already applied locally.
		string id = GetLightIdentifier(lights);

		if (!Replication.IsRunning())
		{
			return;
		}
		if (id.IsEmpty())
		{
			return;
		}

		m_mBeamAngles.Set(id, angle);
		Rpc(Rpc_AskSetBeamAngle, id, angle);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void Rpc_AskSetBeamAngle(string identifier, int angle)
	{
		m_mBeamAngles.Set(identifier, angle);

		ACM_NVL_LightsComponent lights = FindLightsComponentByIdentifier(identifier);
		if (lights)
			lights.SetConeAngleFromNetwork(angle);
		else
		{
			SchedulePendingApply(identifier);
		}

		Rpc(Rpc_DoSetBeamAngle, identifier, angle);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast, RplCondition.NoOwner)]
	protected void Rpc_DoSetBeamAngle(string identifier, int angle)
	{
		ACM_NVL_LightsComponent lights = FindLightsComponentByIdentifier(identifier);
		if (lights)
			lights.SetConeAngleFromNetwork(angle);
		else
		{
			SchedulePendingApply(identifier);
		}
	}
	
	//------------------------------------------------------------------------------------------------
	// IR Filter/Cover Controls - PER-COMPONENT using string Light Identifier
	//------------------------------------------------------------------------------------------------
	void AskSetIRFilterRemoved(ACM_NVL_LightsComponent lights, bool removed)
	{
		if (!lights) return;
		lights.SetIRFilterRemoved(removed);
		
		string id = GetLightIdentifier(lights);
		if (!id.IsEmpty())
			m_mIRFilterRemoved.Set(id, removed);
		
		if (Replication.IsRunning() && !id.IsEmpty()) 
			Rpc(Rpc_AskSetIRFilterRemoved, id, removed);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void Rpc_AskSetIRFilterRemoved(string identifier, bool removed)
	{
		m_mIRFilterRemoved.Set(identifier, removed);
		
		ACM_NVL_LightsComponent lights = FindLightsComponentByIdentifier(identifier);
		if (lights)
			lights.SetIRFilterRemovedFromNetwork(removed);
		
		Rpc(Rpc_DoSetIRFilterRemoved, identifier, removed);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast, RplCondition.NoOwner)]
	protected void Rpc_DoSetIRFilterRemoved(string identifier, bool removed)
	{
		ACM_NVL_LightsComponent lights = FindLightsComponentByIdentifier(identifier);
		if (lights)
			lights.SetIRFilterRemovedFromNetwork(removed);
	}

	protected void FindAllLightsOnCharacter(out array<ACM_NVL_LightsComponent> outLights)
	{
		IEntity owner = GetOwner();
		if (!owner) return;
		
		FindLightsRecursive(owner, outLights);
		
		ChimeraCharacter char = ChimeraCharacter.Cast(owner);
		if (char)
		{
			BaseWeaponManagerComponent weaponMgr = BaseWeaponManagerComponent.Cast(char.FindComponent(BaseWeaponManagerComponent));
			if (weaponMgr)
			{
				array<IEntity> weapons = {};
				weaponMgr.GetWeaponsList(weapons);
				foreach (IEntity weapon : weapons)
				{
					if (weapon)
						FindLightsRecursive(weapon, outLights);
				}
			}
		}
	}
	
	protected void FindLightsRecursive(IEntity entity, out array<ACM_NVL_LightsComponent> outArray, int depth = 0)
	{
		if (!entity || depth > MAX_LIGHT_SEARCH_DEPTH)
			return;

		array<Managed> lightComponents = {};
		entity.FindComponents(ACM_NVL_LightsComponent, lightComponents);
		foreach (Managed lightManaged : lightComponents)
		{
			ACM_NVL_LightsComponent comp = ACM_NVL_LightsComponent.Cast(lightManaged);
			if (comp && outArray.Find(comp) == -1)
				outArray.Insert(comp);
		}

		array<Managed> attachmentSlots = {};
		entity.FindComponents(AttachmentSlotComponent, attachmentSlots);
		foreach (Managed slotManaged : attachmentSlots)
		{
			AttachmentSlotComponent slot = AttachmentSlotComponent.Cast(slotManaged);
			if (!slot)
				continue;

			IEntity attached = slot.GetAttachedEntity();
			if (attached)
				FindLightsRecursive(attached, outArray, depth + 1);
		}

		BaseInventoryStorageComponent storage = BaseInventoryStorageComponent.Cast(entity.FindComponent(BaseInventoryStorageComponent));
		if (storage)
		{
			array<IEntity> items = {};
			storage.GetAll(items);
			foreach (IEntity item : items)
			{
				if (item)
					FindLightsRecursive(item, outArray, depth + 1);
			}
		}

		IEntity child = entity.GetChildren();
		while (child)
		{
			FindLightsRecursive(child, outArray, depth + 1);
			child = child.GetSibling();
		}
	}

	//------------------------------------------------------------------------------------------------
	// NVG Controls
	//------------------------------------------------------------------------------------------------
	void AskSetNVG(ACM_NightVisionComponent nvg, bool state)
	{
		if (!nvg) return;
		nvg.OnToggleActive(state);
		if (Replication.IsRunning()) Rpc(Rpc_AskSetNVG, state);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void Rpc_AskSetNVG(bool enabled)
	{
		ACM_NightVisionComponent nvg = FindNVGOnCharacter();
		if (nvg) nvg.OnToggleActive(enabled);
		Rpc(Rpc_DoSetNVG, enabled);
	}

	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast, RplCondition.NoOwner)]
	protected void Rpc_DoSetNVG(bool enabled)
	{
		ACM_NightVisionComponent nvg = FindNVGOnCharacter();
		if (nvg) nvg.OnToggleActive(enabled);
	}

	protected ACM_NightVisionComponent FindNVGOnCharacter()
	{
		IEntity owner = GetOwner();
		if (!owner) return null;
		return ACM_NightVisionComponent.Cast(FindComponentRecursive(owner, ACM_NightVisionComponent));
	}

	//------------------------------------------------------------------------------------------------
	// ApplyStoredStateToComponent
	// Push all stored JIP/live state from this component's maps onto a specific lights component.
	// Called from RegisterLightsComponent (on late registration) and from retry paths.
	// Deliberate apply order: color → brightness → beam → filter → flashlight → IR
	//------------------------------------------------------------------------------------------------
	protected void ApplyStoredStateToComponent(ACM_NVL_LightsComponent lights, string id)
	{
		if (!lights || id.IsEmpty()) return;

		string applyFlashBright = "none";
		if (m_mFlashlightBrightness.Contains(id)) applyFlashBright = string.Format("%1", m_mFlashlightBrightness.Get(id));
		string applyIRBright = "none";
		if (m_mIRBrightness.Contains(id)) applyIRBright = string.Format("%1", m_mIRBrightness.Get(id));
		string applyBeam = "none";
		if (m_mLightColorIndices.Contains(id))
			lights.SetLightColorFromNetwork(m_mLightColorIndices.Get(id));

		if (m_mFlashlightBrightness.Contains(id))
			lights.SetFlashlightBrightnessFromNetwork(m_mFlashlightBrightness.Get(id));

		if (m_mIRBrightness.Contains(id))
			lights.SetIRBrightnessFromNetwork(m_mIRBrightness.Get(id));

		if (m_mBeamAngles.Contains(id))
			lights.SetConeAngleFromNetwork(m_mBeamAngles.Get(id));

		if (m_mIRFilterRemoved.Contains(id))
			lights.SetIRFilterRemovedFromNetwork(m_mIRFilterRemoved.Get(id));

		if (m_mFlashlightStates.Contains(id))
			lights.ApplyFlashlightState(m_mFlashlightStates.Get(id));

		if (m_mIRStates.Contains(id))
			lights.ApplyIRState(m_mIRStates.Get(id));
	}

	//------------------------------------------------------------------------------------------------
	// Short-retry mechanism for RPCs that arrive before the target component has registered.
	// Stores the identifier and schedules RetryPendingApplies after a short delay.
	// At most MAX_RPC_RETRIES attempts \u2013 not an infinite loop.
	//------------------------------------------------------------------------------------------------
	protected ref map<string, int> m_mPendingRetryIds = new map<string, int>();
	protected static const int MAX_RPC_RETRIES = 4;

	protected void SchedulePendingApply(string id)
	{
		if (id.IsEmpty()) return;
		if (!m_mPendingRetryIds.Contains(id))
		{
			m_mPendingRetryIds.Set(id, 0);
			if (GetGame() && GetGame().GetCallqueue())
				GetGame().GetCallqueue().CallLater(RetryPendingApplies, 400, false);
		}
	}

	protected void RetryPendingApplies()
	{
		array<string> toRemove = {};
		for (int i = 0; i < m_mPendingRetryIds.Count(); i++)
		{
			string id = m_mPendingRetryIds.GetKey(i);
			int retries = m_mPendingRetryIds.GetElement(i);

			ACM_NVL_LightsComponent lights = FindLightsComponentByIdentifier(id);
			if (lights)
			{
				ApplyStoredStateToComponent(lights, id);
				toRemove.Insert(id);
			}
			else if (retries >= MAX_RPC_RETRIES)
			{
				toRemove.Insert(id);
			}
			else
			{
				m_mPendingRetryIds.Set(id, retries + 1);
			}
		}

		foreach (string id : toRemove)
			m_mPendingRetryIds.Remove(id);

		// If there are still pending IDs, schedule another pass
		if (m_mPendingRetryIds.Count() > 0)
		{
			if (GetGame() && GetGame().GetCallqueue())
				GetGame().GetCallqueue().CallLater(RetryPendingApplies, 500, false);
		}
	}

	//------------------------------------------------------------------------------------------------
	// Utility Functions
	//------------------------------------------------------------------------------------------------
	protected ScriptComponent FindComponentRecursive(IEntity entity, typename componentType)
	{
		if (!entity) return null;
		ScriptComponent comp = ScriptComponent.Cast(entity.FindComponent(componentType));
		if (comp) return comp;
		IEntity child = entity.GetChildren();
		while (child)
		{
			ScriptComponent found = FindComponentRecursive(child, componentType);
			if (found) return found;
			child = child.GetSibling();
		}
		return null;
	}
	
	static ACM_NVL_NetworkComponent GetFromEntity(IEntity entity)
	{
		if (!entity) return null;
		IEntity current = entity;
		while (current)
		{
			ACM_NVL_NetworkComponent netComp = ACM_NVL_NetworkComponent.Cast(current.FindComponent(ACM_NVL_NetworkComponent));
			if (netComp) return netComp;
			current = current.GetParent();
		}
		return null;
	}
	
	override void OnDelete(IEntity owner)
	{
		if (GetGame() && GetGame().GetCallqueue())
		{
			GetGame().GetCallqueue().Remove(ApplyAllStatesFromJIP);
			GetGame().GetCallqueue().Remove(RetryPendingApplies);
		}
		super.OnDelete(owner);
	}
}
