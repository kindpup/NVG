//------------------------------------------------------------------------------------------------
// ACM_NVL_LightsComponent.c
// Universal light system for any entity with configurable keybinds
// Integrated with ACM Night Vision system - IR lights only visible when NVG active
// Part of ACM Night Vision and Lights mod
//------------------------------------------------------------------------------------------------

[ComponentEditorProps(category: "ACM/Lights", description: "Universal light system with configurable keybind and NVG integration")]
class ACM_NVL_LightsComponentClass : ScriptComponentClass {}

class ACM_NVL_LightsComponent : ScriptComponent
{
	protected static ref map<string, Resource> s_mPrefabResourceCache = new map<string, Resource>();

	[Attribute("", UIWidgets.EditBox, desc: "Unique identifier for this light (e.g. 'HelmetLight', 'WeaponPEQ15'). Used for network sync when multiple lights exist.")]
	protected string m_sLightIdentifier;

	[Attribute("", UIWidgets.Auto, desc: "Light sources (flashlight / IR / etc.)")]
	protected ref array<ref ACM_NVL_LightSource> m_aSources;

	[Attribute("ACMToggleFlashlight", UIWidgets.EditBox, desc: "Input action name to toggle flashlight")]
	protected string m_sToggleAction;

	[Attribute("ACMToggleIR", UIWidgets.EditBox, desc: "Input action name to toggle IR light")]
	protected string m_sToggleIRAction;
	
	[Attribute("ACM_Light_BrightnessUp", UIWidgets.EditBox, desc: "Input action for brightness increase")]
	protected string m_sBrightnessUpAction;
	
	[Attribute("ACM_Light_BrightnessDown", UIWidgets.EditBox, desc: "Input action for brightness decrease")]
	protected string m_sBrightnessDownAction;

	[Attribute("ACM_Light_BeamWider", UIWidgets.EditBox, desc: "Input action to widen beam cone angle")]
	protected string m_sConeAngleUpAction;

	[Attribute("ACM_Light_BeamNarrow", UIWidgets.EditBox, desc: "Input action to narrow beam cone angle")]
	protected string m_sConeAngleDownAction;

	protected ref map<ACM_NVL_LightSource, IEntity> m_mLights = new map<ACM_NVL_LightSource, IEntity>();
	protected IEntity m_Owner;
	protected bool m_bFlashlightOn;
	protected bool m_bIROn;
	protected bool m_bIRFilterRemoved;  // When true, IR is visible without NVG
	protected bool m_bLightsSpawned;
	protected bool m_bSpawnScheduled;
	protected int m_iCurrentColorIndex;  // Current color index for flashlight
	protected bool m_bIsWeaponActive;    // Track if weapon is currently active/equipped
	protected float m_fLastRespawnTime;

	[Attribute(defvalue: "800", desc: "Polling interval in ms for weapon active-state checks")]
	protected int m_iWeaponStatePollMs;
	
	// Current brightness levels
	protected float m_fCurrentFlashlightBrightness;
	protected float m_fCurrentIRBrightness;
	
	// Current beam cone angle (degrees, applies to flashlight LightEntity sources with cone control enabled)
	protected int m_iCurrentConeAngle;
	
	// Brightness adjustment timing
	protected float m_fLastBrightnessAdjustTime;
	protected float m_fBrightnessAdjustDelay = 0.05; // 50ms between adjustments when holding
	
	// Cone angle adjustment timing (reuses same delay as brightness)
	protected float m_fLastConeAdjustTime;
	
	// Network sync flag - prevents recursive RPC calls
	protected bool m_bSuppressNetworkSync;

	
	// Fallback keyboard keys for MP compatibility
	protected const string FALLBACK_TOGGLE_FLASHLIGHT_KEY = "keyboard:KC_L";
	protected const string FALLBACK_TOGGLE_IR_KEY = "keyboard:KC_SEMICOLON";
	protected const string FALLBACK_BRIGHTNESS_UP_KEY = "keyboard:KC_RBRACKET";
	protected const string FALLBACK_BRIGHTNESS_DOWN_KEY = "keyboard:KC_LBRACKET";
	protected const string SHARED_BRIGHTNESS_UP_ACTION = "ACM_Light_BrightnessUp";
	protected const string SHARED_BRIGHTNESS_DOWN_ACTION = "ACM_Light_BrightnessDown";
	protected const string WEAPON_LIGHT_BRIGHTNESS_UP_ACTION = "ACM_WeaponLight_BrightnessUp";
	protected const string WEAPON_LIGHT_BRIGHTNESS_DOWN_ACTION = "ACM_WeaponLight_BrightnessDown";
	protected const string WEAPON_LASER_BRIGHTNESS_UP_ACTION = "ACM_WeaponLaser_BrightnessUp";
	protected const string WEAPON_LASER_BRIGHTNESS_DOWN_ACTION = "ACM_WeaponLaser_BrightnessDown";
	
	// Fallback keyboard keys for beam cone angle (matches LB_Tweaks convention)
	protected const string FALLBACK_CONE_WIDER_KEY = "keyboard:KC_PERIOD";
	protected const string FALLBACK_CONE_NARROW_KEY = "keyboard:KC_COMMA";
	protected const string SHARED_CONE_WIDER_ACTION = "ACM_Light_BeamWider";
	protected const string SHARED_CONE_NARROW_ACTION = "ACM_Light_BeamNarrow";
	protected const string WEAPON_LIGHT_CONE_WIDER_ACTION = "ACM_WeaponLight_BeamWider";
	protected const string WEAPON_LIGHT_CONE_NARROW_ACTION = "ACM_WeaponLight_BeamNarrow";
	protected const string WEAPON_LASER_CONE_WIDER_ACTION = "ACM_WeaponLaser_BeamWider";
	protected const string WEAPON_LASER_CONE_NARROW_ACTION = "ACM_WeaponLaser_BeamNarrow";
	
	// At max cone width, flashlight output is reduced to this fraction.
	protected const float CONE_BRIGHTNESS_MIN_SCALE = 0.55;
	
	// Available light colors
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

	protected ParametricMaterialInstanceComponent m_EmissiveMat;
	protected SoundComponent m_SoundComp;

	protected bool m_bActionListenerRegistered;
	protected bool m_bToggleFlashlightHeld;
	protected bool m_bToggleIRHeld;
	protected bool m_bNVGActive;
	protected bool m_bInitialized;

	// ---- Readiness state machine ----
	// Prevents network-registration spam and input polling before the attachment
	// is actually in a valid weapon/character ownership chain.
	// States advance as milestones are reached (stored in m_iReadinessState):
	protected static const int RS_LOCAL_INIT             = 0; // DeferredInit done, lights spawned
	protected static const int RS_ATTACHED_NO_CHARACTER  = 1; // Slot/weapon entity confirmed, no character yet
	protected static const int RS_CHARACTER_RESOLVED     = 2; // Character found, no NetworkComponent yet
	protected static const int RS_NETWORK_RESOLVED       = 3; // NetworkComponent found, registering
	protected static const int RS_FULLY_READY            = 4; // Registered, gate open, input active
	protected int m_iReadinessState = RS_LOCAL_INIT;
	// Shortcut gate: set true when RS_FULLY_READY reaches.  Checked every frame.
	protected bool m_bReadinessGateOpen;
	// Frame counter for rate-limiting readiness polling (every 60 frames ~ 2 s).
	protected int m_iReadinessCheckFrame;
	// One-shot guard: input/IR registration only happens once readiness is achieved.
	protected bool m_bPostReadySetupDone;
	// FPS patch: cap input-listener retry loops and throttle EOnFrame work.
	protected int m_iActionListenerRetryCount;
	// 50 x 200ms = 10s budget: MP spawn/loadout application routinely exceeds the old 2s window.
	protected static const int MAX_ACTION_LISTENER_RETRIES = 50;
	protected float m_fFrameWorkAccumulator;
	protected static const float LIGHT_FRAME_WORK_INTERVAL = 0.05;

	// Cached owning character - populated when TryRegisterWithNetworkComponent succeeds (which
	// already walked the replicated inventory chain).  Avoids per-frame weapon-manager scans.
	protected IEntity m_CachedCharacterOwner;

	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		if (SCR_Global.IsEditMode())
			return;

		// ACM DS SAFETY: dedicated servers do not need local NV/light visuals.
		if (Replication.IsRunning() && Replication.IsServer() && !GetGame().GetPlayerController())
			return;
		m_Owner = owner;
		if (m_iWeaponStatePollMs <= 0)
			m_iWeaponStatePollMs = 800;
		
		// Initialize brightness levels here (in case EOnInit never fires on replicated entities)
		m_fCurrentFlashlightBrightness = GetInitialFlashlightBrightness();
		m_fCurrentIRBrightness = GetInitialIRBrightness();
		m_SoundComp = SoundComponent.Cast(owner.FindComponent(SoundComponent));
		m_EmissiveMat = ParametricMaterialInstanceComponent.Cast(owner.FindComponent(ParametricMaterialInstanceComponent));
		
		// Register for INIT and FRAME events
		SetEventMask(owner, EntityEvent.INIT | EntityEvent.FRAME);
		
		// CRITICAL: On dedicated servers, EntityEvent.INIT never fires for entities spawned
		// after world init (e.g. weapon attachments arriving via replication). Schedule
		// DeferredInit here as a guaranteed fallback. The m_bInitialized guard in DeferredInit
		// makes it safe to call multiple times.
		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().CallLater(DeferredInit, 200, false);
	}

	override void EOnInit(IEntity owner)
	{
		m_Owner = owner;
		if (m_iWeaponStatePollMs <= 0)
			m_iWeaponStatePollMs = 800;

		if (SCR_Global.IsEditMode())
			return;

		// ACM DS SAFETY: dedicated servers do not need local NV/light visuals.
		if (Replication.IsRunning() && Replication.IsServer() && !GetGame().GetPlayerController())
			return;
		// Initialize brightness levels - use first source's default
		m_fCurrentFlashlightBrightness = GetInitialFlashlightBrightness();
		m_fCurrentIRBrightness = GetInitialIRBrightness();

		m_SoundComp = SoundComponent.Cast(owner.FindComponent(SoundComponent));
		m_EmissiveMat = ParametricMaterialInstanceComponent.Cast(owner.FindComponent(ParametricMaterialInstanceComponent));

		// Defer initialization to allow replication to settle
		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().CallLater(DeferredInit, 100, false);
	}
	
	//------------------------------------------------------------------------------------------------
	// Called when weapon is equipped/unholstered - restore light states
	//------------------------------------------------------------------------------------------------
	void OnWeaponEquipped()
	{
		// Removed verbose logging - only log warnings/errors
		
		// Refresh NVG state from IR system
		ACM_IRSystem irSys = ACM_IRSystem.GetInstance();
		if (irSys)
			m_bNVGActive = irSys.IsIRVisible();
		
		// Check if lights need to be respawned (parent relationship broken)
		bool needsRespawn = false;
		
		if (m_mLights && m_mLights.Count() > 0)
		{
			for (int i = 0; i < m_mLights.Count(); i++)
			{
				IEntity entity = m_mLights.GetElement(i);
				if (!entity || entity.GetParent() != m_Owner)
				{
					IEntity parentEnt = null;
					if (entity)
						parentEnt = entity.GetParent();
					// Removed verbose logging - only log warnings/errors
					needsRespawn = true;
					break;
				}
			}
		}
		else if (m_aSources && m_aSources.Count() > 0)
		{
			// Removed verbose logging - only log warnings/errors
			needsRespawn = true;
		}
		
		if (needsRespawn)
		{
			float now = System.GetTickCount() / 1000.0;
			if (now - m_fLastRespawnTime < 0.75)
			{
				ApplyAllCurrentState();
				return;
			}

			// Store current states to restore after respawn
			bool wasFlashlightOn = m_bFlashlightOn;
			bool wasIROn = m_bIROn;
			bool wasFilterRemoved = m_bIRFilterRemoved;
			int colorIndex = m_iCurrentColorIndex;
			
			// Clean up old lights
			if (m_mLights)
			{
				for (int i = 0; i < m_mLights.Count(); i++)
				{
					IEntity entity = m_mLights.GetElement(i);
					if (entity)
						SCR_EntityHelper.DeleteEntityAndChildren(entity);
				}
				m_mLights.Clear();
			}
			
			// Restore state flags BEFORE calling SpawnLights so that ApplyAllCurrentState
			// (called at the end of SpawnLights) can re-enable lights with the correct state.
			m_bLightsSpawned = false;
			m_bFlashlightOn = wasFlashlightOn;
			m_bIROn = wasIROn;
			m_iCurrentColorIndex = colorIndex;
			m_bIRFilterRemoved = wasFilterRemoved;
			m_fLastRespawnTime = now;
			
			// SpawnLights -> ApplyAllCurrentState handles filter propagation, cone angle,
			// flashlight on/off, and IR on/off in the correct order.
			SpawnLights();
		}
		else
		{
			// Re-apply all current state in case something was inadvertently reset
			ApplyAllCurrentState();
		}
	}

	protected void DeferredInit()
	{
		if (m_bInitialized || !m_Owner)
			return;

		m_bInitialized = true;

		// ---- Phase A: local state only ----
		// Initialize cone angle BEFORE spawning lights so that SpawnLights / ApplyAllCurrentState
		// see the correct default angle on the very first call.
		m_iCurrentConeAngle = GetEffectiveDefaultConeAngle();

		// Spawn lights if needed
		if (m_aSources && !m_aSources.IsEmpty() && !m_bLightsSpawned && !m_bSpawnScheduled)
		{
			m_bSpawnScheduled = true;
			SpawnLights();
		}

		// ---- Phase B deferred: registration and input listeners ----
		// For non-weapon equipment: register listeners early so toggle inputs work before
		// network registration completes.  RegisterActionListeners has its own ownership retry
		// loop and won't double-register (m_bActionListenerRegistered guard).
		//
		// Weapon components deliberately skip this early path.  Their weapon->character ownership
		// chain is not valid at DeferredInit time (attachment may have no slot owner yet).
		// Listeners are registered later via EnsurePostReadySetup() once FULLY_READY is reached.
		if (!IsOnWeapon() && GetGame() && GetGame().GetCallqueue())
		{
			if (!Replication.IsRunning() || !Replication.IsServer())
				GetGame().GetCallqueue().CallLater(RegisterActionListeners, 100, false);
		}

		m_iReadinessState = RS_LOCAL_INIT;
		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().CallLater(CheckReadiness, 300, false);
	}
	
	//------------------------------------------------------------------------------------------------
	// Frame-based polling for brightness input (fallback for MP where listeners may not fire)
	//------------------------------------------------------------------------------------------------
	override void EOnFrame(IEntity owner, float timeSlice)
	{
		// ACM DS SAFETY: no visual/input frame processing on dedicated server.
		if (Replication.IsRunning() && Replication.IsServer() && !GetGame().GetPlayerController())
			return;

		// Guaranteed init path: EOnFrame always fires once the entity is in-world.
		if (!m_bInitialized)
			DeferredInit();

		if (!m_bInitialized)
			return;

		// FPS patch: once a replicated component is ready and confirmed remote, stop FRAME work.
		// Network state still applies through RPC/JIP setters; only local-owner input needs frame fallback.
		if (m_bReadinessGateOpen && !IsLocalPlayerOwnerCached())
		{
			SetEventMask(owner, EntityEvent.INIT);
			return;
		}

		// FPS patch: throttle fallback/readiness frame work to 20 Hz instead of every rendered frame.
		m_fFrameWorkAccumulator += timeSlice;
		if (m_fFrameWorkAccumulator < LIGHT_FRAME_WORK_INTERVAL)
			return;
		m_fFrameWorkAccumulator = 0;

		// Readiness gate: continue polling CheckReadiness until FULLY_READY.
		if (!m_bReadinessGateOpen)
		{
			m_iReadinessCheckFrame++;
			if (m_iReadinessCheckFrame >= 60)
			{
				m_iReadinessCheckFrame = 0;
				CheckReadiness();
			}
		}
		else
		{
			PollWeaponToggleInputs();
		}

		if (ShouldPollWeaponInput())
		{
			PollBrightnessInput();
			PollConeInput();
		}
	}

	//------------------------------------------------------------------------------------------------
	// Returns a log-tag prefix used by CheckReadiness for all state-transition messages.
	// Subclasses override this to emit component-type-specific log prefixes
	// (e.g. "ACM_WeaponReady" for weapon components, "ACM_EquipReady" for equipment).
	//------------------------------------------------------------------------------------------------
	protected string GetReadinessLogTag() { return "ACM_Ready"; }

	//------------------------------------------------------------------------------------------------
	// Readiness state machine - Phase B activation.
	// Called on first DeferredInit and then every ~2 s from EOnFrame until FULLY_READY.
	// Advances through milestone states; transitions are logged once each.
	//   RS_LOCAL_INIT -> (weapon only) RS_ATTACHED_NO_CHARACTER
	//                 -> RS_CHARACTER_RESOLVED -> RS_NETWORK_RESOLVED -> RS_FULLY_READY
	//------------------------------------------------------------------------------------------------
	protected void CheckReadiness()
	{
		if (!m_bInitialized || !m_Owner)
			return;

		// Already fully ready - nothing to do.
		if (m_bReadinessGateOpen)
			return;

		// ---- State RS_LOCAL_INIT -> RS_ATTACHED_NO_CHARACTER (weapon path only) ----
		// For weapon attachments verify the inventory slot is populated before attempting
		// character resolution.  Equipment items (IsOnWeapon()=false) skip this gate and
		// proceed directly to character resolution from RS_LOCAL_INIT.
		if (m_iReadinessState == RS_LOCAL_INIT && IsOnWeapon())
		{
			bool ownerIsWeaponEntity = (WeaponComponent.Cast(m_Owner.FindComponent(WeaponComponent)) != null);
			if (!ownerIsWeaponEntity)
			{
				InventoryItemComponent ic = InventoryItemComponent.Cast(m_Owner.FindComponent(InventoryItemComponent));
				InventoryStorageSlot parentSlot = null;
				if (ic) parentSlot = ic.GetParentSlot();
				IEntity slotOwner = null;
				if (parentSlot) slotOwner = parentSlot.GetOwner();
				if (!slotOwner)
					return; // no slot yet - no log spam, retried every ~2 s from EOnFrame
			}

			// Slot confirmed (or owner is the weapon entity itself).
			m_iReadinessState = RS_ATTACHED_NO_CHARACTER;
			// Fall through to character resolution.
		}

		// ---- -> RS_CHARACTER_RESOLVED ----
		// Equipment items reach here directly from RS_LOCAL_INIT.
		// Weapon attachments reach here after slot confirmation above.
		IEntity character = GetCharacterOwner();
		if (!character)
			return;

		if (m_iReadinessState < RS_CHARACTER_RESOLVED)
		{
			m_iReadinessState = RS_CHARACTER_RESOLVED;
			// Fall through to NetworkComponent check.
		}

		// ---- -> RS_NETWORK_RESOLVED ----
		ACM_NVL_NetworkComponent netComp = ACM_NVL_NetworkComponent.Cast(character.FindComponent(ACM_NVL_NetworkComponent));
		if (!netComp)
			netComp = FindNetworkComponent();
		if (!netComp)
			return;

		if (m_iReadinessState < RS_NETWORK_RESOLVED)
		{
			m_iReadinessState = RS_NETWORK_RESOLVED;
			// Fall through to registration.
		}

		// ---- -> RS_FULLY_READY: validate identifier and register ----
		string id = GetResolvedNetworkIdentifier();
		if (id.IsEmpty())
			return;

		if (!m_sRegisteredNetworkId.IsEmpty() && m_sRegisteredNetworkId != id)
			netComp.UnregisterLightsComponent(m_sRegisteredNetworkId);

		m_mNetworkComponentRef = netComp;
		m_sRegisteredNetworkId = id;
		netComp.RegisterLightsComponent(this, id);
		m_CachedCharacterOwner = character;

		m_iReadinessState  = RS_FULLY_READY;
		m_bReadinessGateOpen = true;

		// Post-ready one-time setup: action listeners, IR system, weapon state timer.
		EnsurePostReadySetup();
	}

	//------------------------------------------------------------------------------------------------
	// Poll weapon toggle inputs as a fallback when dedicated-client listeners do not fire.
	//------------------------------------------------------------------------------------------------
	protected void PollWeaponToggleInputs()
	{
		if (!IsOnWeapon())
		{
			m_bToggleFlashlightHeld = false;
			m_bToggleIRHeld = false;
			return;
		}

		if (!IsLocalPlayerOwnerCached())
		{
			m_bToggleFlashlightHeld = false;
			m_bToggleIRHeld = false;
			return;
		}

		InputManager im = GetGame().GetInputManager();
		if (!im)
			return;

		bool toggleFlashlightHeld = false;
		if (!m_sToggleAction.IsEmpty())
		{
			float toggleActionValue = im.GetActionValue(m_sToggleAction);
			if (!m_bActionListenerRegistered && toggleActionValue > 0.5)
				toggleFlashlightHeld = true;
			else if (toggleActionValue <= 0.01 && im.GetActionValue(FALLBACK_TOGGLE_FLASHLIGHT_KEY) > 0.5)
				toggleFlashlightHeld = true;
		}

		if (toggleFlashlightHeld && !m_bToggleFlashlightHeld)
			RequestToggleFlashlight();
		m_bToggleFlashlightHeld = toggleFlashlightHeld;

		bool toggleIRHeld = false;
		if (!m_sToggleIRAction.IsEmpty())
		{
			float toggleIRActionValue = im.GetActionValue(m_sToggleIRAction);
			if (!m_bActionListenerRegistered && toggleIRActionValue > 0.5)
				toggleIRHeld = true;
			else if (toggleIRActionValue <= 0.01 && im.GetActionValue(FALLBACK_TOGGLE_IR_KEY) > 0.5)
				toggleIRHeld = true;
		}

		if (toggleIRHeld && !m_bToggleIRHeld)
			RequestToggleIR();
		m_bToggleIRHeld = toggleIRHeld;
	}
	
	//------------------------------------------------------------------------------------------------
	// Poll brightness input from actions (with raw keyboard fallback for MP)
	//------------------------------------------------------------------------------------------------
	protected void PollBrightnessInput()
	{
		if (!m_bReadinessGateOpen)
			return;
		if (!IsLocalPlayerOwnerCached())
			return;
		
		// Only adjust if a light is on
		if (!m_bFlashlightOn && !m_bIROn)
			return;
		
		// Rate limit
		float currentTime = System.GetTickCount() / 1000.0;
		if (currentTime - m_fLastBrightnessAdjustTime < m_fBrightnessAdjustDelay)
			return;
		
		InputManager im = GetGame().GetInputManager();
		if (!im)
			return;
		
		// Read action values
		float upValue = GetAdjustmentActionValue(im, m_sBrightnessUpAction, SHARED_BRIGHTNESS_UP_ACTION, WEAPON_LIGHT_BRIGHTNESS_UP_ACTION, WEAPON_LASER_BRIGHTNESS_UP_ACTION);
		float downValue = GetAdjustmentActionValue(im, m_sBrightnessDownAction, SHARED_BRIGHTNESS_DOWN_ACTION, WEAPON_LIGHT_BRIGHTNESS_DOWN_ACTION, WEAPON_LASER_BRIGHTNESS_DOWN_ACTION);
		
		// Fallback to raw keyboard if actions return 0 (MP compatibility)
		if (upValue <= 0.01)
			upValue = im.GetActionValue(FALLBACK_BRIGHTNESS_UP_KEY);
		if (downValue <= 0.01)
			downValue = im.GetActionValue(FALLBACK_BRIGHTNESS_DOWN_KEY);
		
		if (upValue > 0.5)
		{
			m_fLastBrightnessAdjustTime = currentTime;
			AdjustBrightness(true);
		}
		else if (downValue > 0.5)
		{
			m_fLastBrightnessAdjustTime = currentTime;
			AdjustBrightness(false);
		}
	}
	
	//------------------------------------------------------------------------------------------------
	// Poll beam cone angle input (. = widen, , = narrow)
	// Mirrors PollBrightnessInput exactly so it works on dedicated clients.
	//------------------------------------------------------------------------------------------------
	protected void PollConeInput()
	{
		if (!m_bReadinessGateOpen)
			return;
		if (!IsLocalPlayerOwnerCached())
			return;

		// Only if any light is on
		if (!m_bFlashlightOn && !m_bIROn)
			return;

		if (!SupportsConeControl())
			return;

		// Rate limit
		float currentTime = System.GetTickCount() / 1000.0;
		if (currentTime - m_fLastConeAdjustTime < m_fBrightnessAdjustDelay)
			return;

		InputManager im = GetGame().GetInputManager();
		if (!im)
			return;

		// Read from all possible action names (primary -> shared -> weapon-type-specific -> raw key)
		float widerValue  = GetAdjustmentActionValue(im, m_sConeAngleUpAction,   SHARED_CONE_WIDER_ACTION,  WEAPON_LIGHT_CONE_WIDER_ACTION,  WEAPON_LASER_CONE_WIDER_ACTION);
		float narrowValue = GetAdjustmentActionValue(im, m_sConeAngleDownAction, SHARED_CONE_NARROW_ACTION, WEAPON_LIGHT_CONE_NARROW_ACTION, WEAPON_LASER_CONE_NARROW_ACTION);

		// Raw keyboard fallback - same as brightness path
		if (widerValue  <= 0.01) widerValue  = im.GetActionValue(FALLBACK_CONE_WIDER_KEY);
		if (narrowValue <= 0.01) narrowValue = im.GetActionValue(FALLBACK_CONE_NARROW_KEY);

		if (widerValue > 0.5)
		{
			m_fLastConeAdjustTime = currentTime;
			AdjustConeAngle(true);
		}
		else if (narrowValue > 0.5)
		{
			m_fLastConeAdjustTime = currentTime;
			AdjustConeAngle(false);
		}
	}

	protected float GetAdjustmentActionValue(InputManager im, string primaryAction, string sharedAction, string weaponLightAction, string weaponLaserAction)
	{
		if (!im)
			return 0;

		float value = 0;
		if (!primaryAction.IsEmpty())
			value = im.GetActionValue(primaryAction);
		if (value > 0.01)
			return value;

		if (!sharedAction.IsEmpty() && sharedAction != primaryAction)
		{
			value = im.GetActionValue(sharedAction);
			if (value > 0.01)
				return value;
		}

		if (ACM_NVL_WeaponLightComponent.Cast(this) && !weaponLightAction.IsEmpty() && weaponLightAction != primaryAction)
		{
			value = im.GetActionValue(weaponLightAction);
			if (value > 0.01)
				return value;
		}

		if (ACM_NVL_WeaponLaserComponent.Cast(this) && !weaponLaserAction.IsEmpty() && weaponLaserAction != primaryAction)
		{
			value = im.GetActionValue(weaponLaserAction);
			if (value > 0.01)
				return value;
		}

		return 0;
	}

	//------------------------------------------------------------------------------------------------
	// Check if this component is on a weapon
	//------------------------------------------------------------------------------------------------
	protected bool IsOnWeapon()
	{
		if (!m_Owner)
			return false;
		
		// Check if owner is a weapon or has weapon component
		if (WeaponComponent.Cast(m_Owner.FindComponent(WeaponComponent)))
			return true;
		
		// Check if parent is a weapon
		IEntity parent = m_Owner.GetParent();
		if (parent && WeaponComponent.Cast(parent.FindComponent(WeaponComponent)))
			return true;
		
		return false;
	}

	//------------------------------------------------------------------------------------------------
	// Readiness and replication helpers
	//
	// These are the single source of truth for all gating decisions in brightness, beam,
	// and input-listener code.  Subclasses may override where noted.
	//------------------------------------------------------------------------------------------------

	// True when the readiness state machine has reached RS_FULLY_READY.
	protected bool IsFullyReady()
	{
		return m_bReadinessGateOpen;
	}

	// True when the weapon attachment has a valid parent-slot owner or the owner IS a weapon.
	// Always returns true for non-weapon (equipment) components.
	protected bool HasValidAttachmentChain()
	{
		if (!IsOnWeapon()) return true;
		if (!m_Owner) return false;
		if (WeaponComponent.Cast(m_Owner.FindComponent(WeaponComponent)))
			return true;
		InventoryItemComponent ic = InventoryItemComponent.Cast(m_Owner.FindComponent(InventoryItemComponent));
		if (!ic) return false;
		InventoryStorageSlot slot = ic.GetParentSlot();
		return slot != null && slot.GetOwner() != null;
	}

	// True when a character owner can be quickly resolved (cache-first).
	protected bool HasResolvedCharacterOwner()
	{
		if (m_CachedCharacterOwner) return true;
		return GetCharacterOwner() != null;
	}

	// True when a NetworkComponent has been found and is cached.
	protected bool HasResolvedNetworkComponent()
	{
		if (m_mNetworkComponentRef) return true;
		return FindNetworkComponent() != null;
	}

	// Whether brightness/cone polling should run this frame.
	// Non-weapon (equipment) components: when initialized AND not a dedicated server.
	//   On DS there is no local player owner, so entering the poll functions every frame
	//   just to hit IsLocalPlayerOwnerCached()=false is wasted work - skip at the gate.
	// Weapon components: delegates to CanProcessUserInput() for fast cached-char check.
	protected bool ShouldPollWeaponInput()
	{
		// FPS patch: all per-frame input polling is local-owner only.
		return CanProcessUserInput();
	}

	// Ownership check using the character cached at FULLY_READY time.
	// Compares against SCR_PlayerController.GetControlledEntity() - no inventory chain re-walk.
	// Mirrors the early-exit pattern in IsLocalPlayerOwner() to avoid calling GetPlayerController()
	// on a dedicated server where no local player exists (same guard as the original function).
	protected bool IsLocalPlayerOwnerCached()
	{
		// No local player on a dedicated server - never owner.
		if (Replication.IsRunning() && Replication.IsServer())
			return false;

		IEntity charOwner = m_CachedCharacterOwner;
		if (!charOwner)
			return false;

		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return false;

		SCR_PlayerController scrPC = SCR_PlayerController.Cast(pc);
		if (!scrPC)
			return false;

		return scrPC.GetControlledEntity() == charOwner;
	}

	// Whether a local-player input action (brightness, beam, toggle) should be processed.
	// Uses IsLocalPlayerOwnerCached() so no inventory chain re-walk occurs on each input event.
	protected bool CanProcessUserInput()
	{
		if (!m_bReadinessGateOpen)
			return false;
		return IsLocalPlayerOwnerCached();
	}

	// Full sync-readiness check used by weapon AdjustBrightness / AdjustConeAngle.
	// Returns true only when ALL of the following hold:
	//   - FULLY_READY reached (readiness gate open)
	//   - stable registered identifier is non-empty
	//   - NetworkComponent reference is cached
	//   - Enforce replication stack is running
	// Using this helper removes the need for four individual gate checks in each adjust method.
	protected bool IsReplicationReadyForControlSync()
	{
		if (!m_bReadinessGateOpen) return false;
		if (m_sRegisteredNetworkId.IsEmpty()) return false;
		if (!m_mNetworkComponentRef) return false;
		if (!Replication.IsRunning()) return false;
		return true;
	}

	// One-time post-ready setup: registers action listeners, IR system, and (for weapons)
	// starts the CheckWeaponState timer.  Safe to call multiple times - guarded by flag.
	protected void EnsurePostReadySetup()
	{
		if (m_bPostReadySetupDone) return;
		m_bPostReadySetupDone = true;

		// Fresh retry budget: the early registration attempts from DeferredInit may have
		// exhausted the counter before the character/controller were resolvable. Now that
		// we are FULLY_READY, registration deserves a full window of its own.
		m_iActionListenerRetryCount = 0;

		if (GetGame() && GetGame().GetCallqueue())
		{
			if (!Replication.IsRunning() || !Replication.IsServer())
				GetGame().GetCallqueue().CallLater(RegisterActionListeners, 100, false);

			GetGame().GetCallqueue().CallLater(RegisterWithIRSystem, 150, false);

			// CheckWeaponState polls the weapon-manager to detect equip/holster events.
			// No local weapon-manager is meaningful on a dedicated server (no local controller),
			// so skip scheduling entirely on DS to avoid 500ms chain-walking for no benefit.
			if (IsOnWeapon() && (!Replication.IsRunning() || !Replication.IsServer()))
				GetGame().GetCallqueue().CallLater(CheckWeaponState, m_iWeaponStatePollMs, true);
		}
	}

	protected Resource ACM_GetPrefabResourceCached(ResourceName prefab)
	{
		if (prefab.IsEmpty())
			return null;

		string key = prefab;
		Resource cached;
		if (s_mPrefabResourceCache.Find(key, cached) && cached && cached.IsValid())
			return cached;

		Resource loaded = Resource.Load(prefab);
		if (loaded && loaded.IsValid())
			s_mPrefabResourceCache.Set(key, loaded);

		return loaded;
	}

	//------------------------------------------------------------------------------------------------
	// Periodically check if weapon state changed (equipped/holstered)
	//------------------------------------------------------------------------------------------------
	protected void CheckWeaponState()
	{
		if (!m_Owner)
			return;
		
		// Get character owner
		IEntity character = GetCharacterOwner();
		if (!character)
			return;
		
		// Get weapon manager
		BaseWeaponManagerComponent weaponMgr = BaseWeaponManagerComponent.Cast(character.FindComponent(BaseWeaponManagerComponent));
		if (!weaponMgr)
			return;
		
		// Check if our weapon is the current weapon
		BaseWeaponComponent currentWeaponComp = weaponMgr.GetCurrentWeapon();
		if (!currentWeaponComp)
		{
			// No weapon equipped
			if (m_bIsWeaponActive)
			{
				m_bIsWeaponActive = false;
				// Removed verbose logging - only log warnings/errors
			}
			return;
		}
		
		// Get the weapon entity from the component
		IEntity currentWeapon = currentWeaponComp.GetOwner();
		if (!currentWeapon)
		{
			if (m_bIsWeaponActive)
			{
				m_bIsWeaponActive = false;
			}
			return;
		}
		
		// Check if current weapon is our owner or parent of our owner
		bool isOurWeapon = (currentWeapon == m_Owner || currentWeapon == m_Owner.GetParent());
		
		// Also check if owner is a child of current weapon (attachment case)
		if (!isOurWeapon)
		{
			IEntity parent = m_Owner;
			while (parent)
			{
				if (parent == currentWeapon)
				{
					isOurWeapon = true;
					break;
				}
				parent = parent.GetParent();
			}
		}
		
		if (isOurWeapon && !m_bIsWeaponActive)
		{
			// Weapon just became active
			m_bIsWeaponActive = true;
			// Removed verbose logging - only log warnings/errors
			OnWeaponEquipped();
		}
		else if (!isOurWeapon && m_bIsWeaponActive)
		{
			// Weapon became inactive
			m_bIsWeaponActive = false;
		}
	}
	
	protected void RegisterWithIRSystem()
	{
		ACM_IRSystem sys = ACM_IRSystem.GetInstance();
		if (sys)
			sys.AddNVLLightsComponent(this);
	}
	
	protected void UnregisterFromIRSystem()
	{
		ACM_IRSystem sys = ACM_IRSystem.GetInstance();
		if (sys)
			sys.RemoveNVLLightsComponent(this);
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
		
		// Removed verbose logging - only log warnings/errors

		foreach (ACM_NVL_LightSource src : m_aSources)
		{
			if (!src)
			{
				continue;
			}
			
			ResourceName prefab = src.GetPrefab();
			if (prefab.IsEmpty())
			{
				continue;
			}
			
			// Removed verbose logging - only log warnings/errors

			Resource res = ACM_GetPrefabResourceCached(prefab);
			if (!res || !res.IsValid())
			{
				continue;
			}

			IEntity spawnedEntity = GetGame().SpawnEntityPrefabLocal(res);
			if (!spawnedEntity)
				continue;
			
			// Check if it's a ACM_IRLaser (GenericEntity) or LightEntity
			ACM_IRLaser irLaser = ACM_IRLaser.Cast(spawnedEntity);
			LightEntity light = LightEntity.Cast(spawnedEntity);

			PointInfo offset = src.GetOffset();
			if (!offset)
			{
				SCR_EntityHelper.DeleteEntityAndChildren(spawnedEntity);
				continue;
			}

			TNodeId pivot = offset.GetNodeId();
			m_Owner.AddChild(spawnedEntity, pivot, EAddChildFlags.AUTO_TRANSFORM);

			vector xf[4];
			offset.GetLocalTransform(xf);
			spawnedEntity.SetLocalTransform(xf);

			// Initialize based on type
			if (irLaser)
			{
				// ACM_IRLaser handles its own visibility - force hidden state
				spawnedEntity.ClearFlags(EntityFlags.VISIBLE | EntityFlags.ACTIVE);
				spawnedEntity.SetScale(0.001); // Make invisible by scale
				irLaser.SetIREnabledWithNVGState(false, false); // Ensure it starts disabled
				// Removed verbose logging - only log warnings/errors
			}
			else if (light)
			{
				light.SetEnabled(false);
				spawnedEntity.ClearFlags(EntityFlags.VISIBLE);
			}

			m_mLights.Insert(src, spawnedEntity);
			
			// Removed verbose logging - only log warnings/errors
		}

		m_bLightsSpawned = true;
		m_bSpawnScheduled = false;
		
		// Reapply all current state now that the light entities exist.
		ApplyAllCurrentState();
	}

	protected IEntity GetOwnerForCharacterResolution()
	{
		if (!m_Owner)
			return null;

		IEntity ownerForResolution = m_Owner;
		InventoryItemComponent ownerItemComp = InventoryItemComponent.Cast(m_Owner.FindComponent(InventoryItemComponent));
		if (ownerItemComp)
		{
			InventoryStorageSlot parentSlot = ownerItemComp.GetParentSlot();
			if (parentSlot && parentSlot.GetOwner())
				ownerForResolution = parentSlot.GetOwner();
		}

		return ownerForResolution;
	}

	protected bool IsEntityInHierarchy(IEntity root, IEntity target, int depth = 0)
	{
		if (!root || !target)
			return false;

		if (root == target)
			return true;

		if (depth >= 20)
			return false;

		array<Managed> attachmentSlots = {};
		root.FindComponents(AttachmentSlotComponent, attachmentSlots);
		foreach (Managed slotManaged : attachmentSlots)
		{
			AttachmentSlotComponent slot = AttachmentSlotComponent.Cast(slotManaged);
			if (!slot)
				continue;

			IEntity attached = slot.GetAttachedEntity();
			if (attached && IsEntityInHierarchy(attached, target, depth + 1))
				return true;
		}

		BaseInventoryStorageComponent storage = BaseInventoryStorageComponent.Cast(root.FindComponent(BaseInventoryStorageComponent));
		if (storage)
		{
			array<IEntity> items = {};
			storage.GetAll(items);
			foreach (IEntity item : items)
			{
				if (item && IsEntityInHierarchy(item, target, depth + 1))
					return true;
			}
		}

		IEntity child = root.GetChildren();
		while (child)
		{
			if (IsEntityInHierarchy(child, target, depth + 1))
				return true;
			child = child.GetSibling();
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	// Server-safe bridge: given a weapon entity (which has no InventoryItemComponent when held),
	// scan all connected player entities through PlayerManager to find the one whose
	// BaseWeaponManagerComponent contains this weapon.  Works on DS, listen-server, and client.
	//------------------------------------------------------------------------------------------------
	protected ChimeraCharacter FindCharacterOwningWeapon(IEntity weaponEnt)
	{
		if (!weaponEnt || !GetGame())
			return null;

		PlayerManager pm = GetGame().GetPlayerManager();
		if (!pm)
			return null;

		array<int> playerIds = {};
		pm.GetPlayers(playerIds);

		foreach (int pid : playerIds)
		{
			SCR_PlayerController scrPC = SCR_PlayerController.Cast(pm.GetPlayerController(pid));
			if (!scrPC)
				continue;

			IEntity playerEnt = scrPC.GetControlledEntity();
			if (!playerEnt)
				continue;

			BaseWeaponManagerComponent wm = BaseWeaponManagerComponent.Cast(playerEnt.FindComponent(BaseWeaponManagerComponent));
			if (!wm)
				continue;

			array<IEntity> weapons = {};
			wm.GetWeaponsList(weapons);
			foreach (IEntity w : weapons)
			{
				if (w == weaponEnt)
				{
					ChimeraCharacter character = ChimeraCharacter.Cast(playerEnt);
					if (character)
						return character;
				}
			}
		}

		return null;
	}

	protected ChimeraCharacter GetCharacterOwner()
	{
		if (!m_Owner)
			return null;

		IEntity ownerForResolution = GetOwnerForCharacterResolution();

		// Path 1: ACM_InventoryUtils - fastest, walks replicated inventory, works on DS.
		ChimeraCharacter inventoryCharacterOwner = ACM_InventoryUtils.FindCharacterOwnerFromEntity(m_Owner);
		if (inventoryCharacterOwner)
			return inventoryCharacterOwner;

		// Path 1b: attachment is on a weapon entity (ownerForResolution = weapon) OR m_Owner itself
		// IS the weapon entity (ACM_NVL_WeaponLightComponent on the weapon).  When the weapon is
		// actively held its IC may have no parent slot, so ownerForResolution == m_Owner - but we
		// still need the PlayerManager scan on DS server where Path 3 (local PC) is unavailable.
		bool ownerIsWeaponEnt = (WeaponComponent.Cast(m_Owner.FindComponent(WeaponComponent)) != null);
		if (IsOnWeapon() && (ownerForResolution != m_Owner || ownerIsWeaponEnt))
		{
			IEntity weaponEnt = ownerForResolution;
			if (ownerIsWeaponEnt)
				weaponEnt = m_Owner;
			ChimeraCharacter weaponChar = ACM_InventoryUtils.FindCharacterOwnerFromEntity(weaponEnt);
			if (!weaponChar)
				weaponChar = FindCharacterOwningWeapon(weaponEnt);
			if (weaponChar)
				return weaponChar;
		}

		// Path 2: Scene parent walk - works for helmet/gear that ARE children of the character entity.
		IEntity current = ownerForResolution;
		while (current)
		{
			if (ChimeraCharacter.Cast(current))
				return ChimeraCharacter.Cast(current);
			current = current.GetParent();
		}
		
		// Path 3: Weapon-attachment path. Weapons are NOT scene-children of the character entity -
		// they are managed by BaseWeaponManagerComponent. Check the local PlayerController.
		// NOTE: On dedicated server, GetPlayerController() returns null (no local PC on DS).
		// That is correct - brightness/beam input should only be processed on the owning CLIENT.
		if (IsOnWeapon())
		{
			PlayerController pc = GetGame().GetPlayerController();
			if (pc)
			{
				SCR_PlayerController scrPC = SCR_PlayerController.Cast(pc);
				if (scrPC)
				{
					IEntity controlled = scrPC.GetControlledEntity();
					if (controlled)
					{
						BaseWeaponManagerComponent weaponMgr = BaseWeaponManagerComponent.Cast(controlled.FindComponent(BaseWeaponManagerComponent));
						if (weaponMgr)
						{
							array<IEntity> weapons = {};
							weaponMgr.GetWeaponsList(weapons);
							foreach (IEntity weapon : weapons)
							{
								if (!weapon)
									continue;
								IEntity check = ownerForResolution;
								while (check)
								{
									if (check == weapon)
									{
										return ChimeraCharacter.Cast(controlled);
									}
									check = check.GetParent();
								}

								if (IsEntityInHierarchy(weapon, m_Owner) || IsEntityInHierarchy(weapon, ownerForResolution))
									return ChimeraCharacter.Cast(controlled);
							}
						}
					}
				}
			}
		}

		return null;
	}
	
	//------------------------------------------------------------------------------------------------
	// Find the NetworkComponent for this light, handling both helmet/gear (parent walk)
	// and weapon attachments (character lookup via weapon manager)
	//------------------------------------------------------------------------------------------------
	// Cached reference to the character's NetworkComponent (set by TryRegisterWithNetworkComponent)
	protected ACM_NVL_NetworkComponent m_mNetworkComponentRef;
	// ID under which this component was last registered - stored so unregistration uses
	// the correct key even if GetResolvedNetworkIdentifier() later changes.
	protected string m_sRegisteredNetworkId;
	// Retry counter for TryRegisterWithNetworkComponent - resets to 0 on success.
	protected int m_iRegistrationRetryCount;
	protected static const int MAX_REGISTRATION_RETRIES = 8;

	//------------------------------------------------------------------------------------------------
	// Try to find + register with the character's NetworkComponent.
	// Now a thin forwarding wrapper kept for compatibility; real logic lives in CheckReadiness().
	//------------------------------------------------------------------------------------------------
	protected void TryRegisterWithNetworkComponent()
	{
		// Gate: skip if already registered.
		if (m_bReadinessGateOpen)
			return;
		CheckReadiness();
	}

	protected ACM_NVL_NetworkComponent FindNetworkComponent()
	{
		if (m_mNetworkComponentRef)
			return m_mNetworkComponentRef;

		// 1. Direct parent walk: works for helmet/gear scene-parented to the character entity.
		ACM_NVL_NetworkComponent netComp = ACM_NVL_NetworkComponent.GetFromEntity(m_Owner);
		if (netComp)
			return netComp;

		// 2. Inventory chain walk: DS-safe, follows replicated slot topology.
		netComp = FindNetworkComponentViaInventoryChain();
		if (netComp)
			return netComp;

		// 3. Last resort: local PlayerController path - only resolves on the owning client.
		//    Kept so non-weapon, non-inventory gear (e.g. editor-placed items) can still register.
		IEntity character = GetCharacterOwner();
		if (character)
		{
			netComp = ACM_NVL_NetworkComponent.Cast(character.FindComponent(ACM_NVL_NetworkComponent));
			if (netComp)
				return netComp;
		}

		return null;
	}

	// Walk up through InventoryItemComponent.GetParentSlot().GetOwner() looking for
	// a ACM_NVL_NetworkComponent on each ancestor entity.  Works on server and remote
	// clients because it uses the replicated inventory topology, not the local PC.
	protected ACM_NVL_NetworkComponent FindNetworkComponentViaInventoryChain()
	{
		if (!m_Owner)
			return null;

		IEntity walkEnt = m_Owner;
		for (int depth = 0; depth < 10; depth++)
		{
			InventoryItemComponent ic = InventoryItemComponent.Cast(walkEnt.FindComponent(InventoryItemComponent));
			if (!ic)
				break;
			InventoryStorageSlot parentSlot = ic.GetParentSlot();
			if (!parentSlot)
				break;
			IEntity slotOwner = parentSlot.GetOwner();
			if (!slotOwner)
				break;

			ACM_NVL_NetworkComponent nc = ACM_NVL_NetworkComponent.Cast(slotOwner.FindComponent(ACM_NVL_NetworkComponent));
			if (nc)
				return nc;

			// If slotOwner is a weapon entity (has WeaponComponent), the inventory chain ends here
			// because held weapons have no InventoryItemComponent pointing up to the character.
			// Bridge: resolve character from the weapon via PlayerManager, then get its NetworkComponent.
			if (WeaponComponent.Cast(slotOwner.FindComponent(WeaponComponent)))
			{
				ChimeraCharacter bridgedChar = ACM_InventoryUtils.FindCharacterOwnerFromEntity(slotOwner);
				if (!bridgedChar)
					bridgedChar = FindCharacterOwningWeapon(slotOwner);

				if (bridgedChar)
				{
					nc = ACM_NVL_NetworkComponent.Cast(bridgedChar.FindComponent(ACM_NVL_NetworkComponent));
					if (nc)
						return nc;
				}
				// Weapon found but character has no NetworkComponent - no point walking further.
				break;
			}

		walkEnt = slotOwner;
		}

		return null;
	}

	protected bool IsLocalPlayerOwner()
	{
		// In editor/listen-server-without-replication: always owner.
		if (!Replication.IsRunning())
			return true;

		// On the dedicated server process itself, there is no local player to own anything.
		// Input processing should never run on DS - return false immediately (no log, expected).
		if (Replication.IsServer())
			return false;

		// Fast path: use cached character set at registration time.
		// Avoids per-frame GetPlayerController() + weapon-manager scan.
		if (m_CachedCharacterOwner)
		{
			PlayerController pc = GetGame().GetPlayerController();
			if (pc)
			{
				SCR_PlayerController scrPC = SCR_PlayerController.Cast(pc);
				if (scrPC && scrPC.GetControlledEntity() == m_CachedCharacterOwner)
					return true;
			}
			// Cached char present but controlled entity doesn't match yet - fall through to full check.
		}

		// On a dedicated CLIENT we need to verify this component belongs to OUR character.
		PlayerController pc2 = GetGame().GetPlayerController();
		if (!pc2)
			return false;

		SCR_PlayerController scrPC2 = SCR_PlayerController.Cast(pc2);
		if (!scrPC2)
			return false;

		IEntity controlled = scrPC2.GetControlledEntity();
		if (!controlled)
			return false;

		IEntity character = GetCharacterOwner();
		if (!character)
			return false;

		bool isOwner = (controlled == character);
		if (isOwner)
			m_CachedCharacterOwner = character;
		return isOwner;
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

		// Never register input on the dedicated server process itself.
		if (Replication.IsRunning() && Replication.IsServer() && !GetGame().GetPlayerController())
			return;

		if (Replication.IsRunning())
		{
			IEntity character = m_CachedCharacterOwner;
			if (!character)
				character = GetCharacterOwner();

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

			// Not the locally controlled character right now. This is frequently a transient
			// spawn/possession race (controlled entity lags behind character resolution), so
			// retry (capped) instead of stopping permanently - permanent stop here killed
			// brightness/beam keybinds for the owning player whenever the race was lost once.
			if (controlled != character)
			{
				// The cached owner may be stale (respawn/possession change) - drop it so the
				// next attempt re-resolves via GetCharacterOwner().
				if (m_CachedCharacterOwner && m_CachedCharacterOwner == character)
					m_CachedCharacterOwner = null;

				ScheduleActionListenerRetry(200);
				return;
			}

			m_CachedCharacterOwner = character;
		}

		InputManager im = GetGame().GetInputManager();
		if (!im)
		{
			return;
		}

		if (!m_sToggleAction.IsEmpty())
			im.AddActionListener(m_sToggleAction, EActionTrigger.DOWN, OnFlashlightKeyPressed);
		
		if (!m_sToggleIRAction.IsEmpty())
			im.AddActionListener(m_sToggleIRAction, EActionTrigger.DOWN, OnIRKeyPressed);
		
		// Brightness adjustment - use PRESSED for repeat while held.
		// Also register shared brightness actions as fallback listeners so that any player
		// who has ACM_Light_BrightnessUp/Down bound keeps working even when running
		// dedicated-input mode that points m_sBrightnessUpAction at the weapon-specific action.
		if (!m_sBrightnessUpAction.IsEmpty())
			im.AddActionListener(m_sBrightnessUpAction, EActionTrigger.PRESSED, OnBrightnessUpPressed);
		if (m_sBrightnessUpAction != SHARED_BRIGHTNESS_UP_ACTION)
			im.AddActionListener(SHARED_BRIGHTNESS_UP_ACTION, EActionTrigger.PRESSED, OnBrightnessUpPressed);

		if (!m_sBrightnessDownAction.IsEmpty())
			im.AddActionListener(m_sBrightnessDownAction, EActionTrigger.PRESSED, OnBrightnessDownPressed);
		if (m_sBrightnessDownAction != SHARED_BRIGHTNESS_DOWN_ACTION)
			im.AddActionListener(SHARED_BRIGHTNESS_DOWN_ACTION, EActionTrigger.PRESSED, OnBrightnessDownPressed);

		if (!m_sConeAngleUpAction.IsEmpty())
			im.AddActionListener(m_sConeAngleUpAction, EActionTrigger.PRESSED, OnConeAngleUpPressed);
		if (m_sConeAngleUpAction != SHARED_CONE_WIDER_ACTION)
			im.AddActionListener(SHARED_CONE_WIDER_ACTION, EActionTrigger.PRESSED, OnConeAngleUpPressed);

		if (!m_sConeAngleDownAction.IsEmpty())
			im.AddActionListener(m_sConeAngleDownAction, EActionTrigger.PRESSED, OnConeAngleDownPressed);
		if (m_sConeAngleDownAction != SHARED_CONE_NARROW_ACTION)
			im.AddActionListener(SHARED_CONE_NARROW_ACTION, EActionTrigger.PRESSED, OnConeAngleDownPressed);

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
			inputManager.RemoveActionListener(m_sToggleAction, EActionTrigger.DOWN, OnFlashlightKeyPressed);
		
		if (!m_sToggleIRAction.IsEmpty())
			inputManager.RemoveActionListener(m_sToggleIRAction, EActionTrigger.DOWN, OnIRKeyPressed);
		
		if (!m_sBrightnessUpAction.IsEmpty())
			inputManager.RemoveActionListener(m_sBrightnessUpAction, EActionTrigger.PRESSED, OnBrightnessUpPressed);
		if (m_sBrightnessUpAction != SHARED_BRIGHTNESS_UP_ACTION)
			inputManager.RemoveActionListener(SHARED_BRIGHTNESS_UP_ACTION, EActionTrigger.PRESSED, OnBrightnessUpPressed);

		if (!m_sBrightnessDownAction.IsEmpty())
			inputManager.RemoveActionListener(m_sBrightnessDownAction, EActionTrigger.PRESSED, OnBrightnessDownPressed);
		if (m_sBrightnessDownAction != SHARED_BRIGHTNESS_DOWN_ACTION)
			inputManager.RemoveActionListener(SHARED_BRIGHTNESS_DOWN_ACTION, EActionTrigger.PRESSED, OnBrightnessDownPressed);

		if (!m_sConeAngleUpAction.IsEmpty())
			inputManager.RemoveActionListener(m_sConeAngleUpAction, EActionTrigger.PRESSED, OnConeAngleUpPressed);
		if (m_sConeAngleUpAction != SHARED_CONE_WIDER_ACTION)
			inputManager.RemoveActionListener(SHARED_CONE_WIDER_ACTION, EActionTrigger.PRESSED, OnConeAngleUpPressed);

		if (!m_sConeAngleDownAction.IsEmpty())
			inputManager.RemoveActionListener(m_sConeAngleDownAction, EActionTrigger.PRESSED, OnConeAngleDownPressed);
		if (m_sConeAngleDownAction != SHARED_CONE_NARROW_ACTION)
			inputManager.RemoveActionListener(SHARED_CONE_NARROW_ACTION, EActionTrigger.PRESSED, OnConeAngleDownPressed);

		m_bActionListenerRegistered = false;
	}

	protected void OnFlashlightKeyPressed()
	{
		if (!CanProcessUserInput())
			return;

		RequestToggleFlashlight();
	}
	
	protected void OnIRKeyPressed()
	{
		if (!CanProcessUserInput())
			return;

		RequestToggleIR();
	}
	
	//------------------------------------------------------------------------------------------------
	// Brightness adjustment handlers
	//------------------------------------------------------------------------------------------------
	protected void OnBrightnessUpPressed()
	{
		if (!CanProcessUserInput())
			return;

		// Only adjust if a light is on
		if (!m_bFlashlightOn && !m_bIROn)
			return;
		
		// Rate limit for smooth continuous adjustment
		float currentTime = System.GetTickCount() / 1000.0;
		if (currentTime - m_fLastBrightnessAdjustTime < m_fBrightnessAdjustDelay)
			return;
		
		m_fLastBrightnessAdjustTime = currentTime;
		AdjustBrightness(true);
	}
	
	protected void OnBrightnessDownPressed()
	{
		if (!CanProcessUserInput())
			return;

		// Only adjust if a light is on
		if (!m_bFlashlightOn && !m_bIROn)
			return;
		
		// Rate limit for smooth continuous adjustment
		float currentTime = System.GetTickCount() / 1000.0;
		if (currentTime - m_fLastBrightnessAdjustTime < m_fBrightnessAdjustDelay)
			return;
		
		m_fLastBrightnessAdjustTime = currentTime;
		AdjustBrightness(false);
	}
	
	protected void OnConeAngleUpPressed()
	{
		if (!CanProcessUserInput())
			return;

		// Mirror PollConeInput: allow beam when flashlight OR IR is on
		if ((!m_bFlashlightOn && !m_bIROn) || !SupportsConeControl())
			return;

		float currentTime = System.GetTickCount() / 1000.0;
		if (currentTime - m_fLastConeAdjustTime < m_fBrightnessAdjustDelay)
			return;

		m_fLastConeAdjustTime = currentTime;
		AdjustConeAngle(true);
	}

	protected void OnConeAngleDownPressed()
	{
		if (!CanProcessUserInput())
			return;

		// Mirror PollConeInput: allow beam when flashlight OR IR is on
		if ((!m_bFlashlightOn && !m_bIROn) || !SupportsConeControl())
			return;

		float currentTime = System.GetTickCount() / 1000.0;
		if (currentTime - m_fLastConeAdjustTime < m_fBrightnessAdjustDelay)
			return;

		m_fLastConeAdjustTime = currentTime;
		AdjustConeAngle(false);
	}
	
	protected void AdjustBrightness(bool increase)
	{
		// Gate 1: not yet FULLY_READY
		if (!m_bReadinessGateOpen)
			return;
		// Gate 2: not the local owning player (cached char avoids re-walking the inventory chain)
		if (!IsLocalPlayerOwnerCached())
			return;
		// Gate 3 (weapon-only): replication must be running - no local-only fallback for weapons.
		if (IsOnWeapon() && !Replication.IsRunning())
			return;

		// Adjust flashlight brightness if on
		if (m_bFlashlightOn)
		{
			// Get step from active sources
			float step = GetEffectiveBrightnessStep(true);
			
			// Finer control at low brightness
			if (m_fCurrentFlashlightBrightness < 1.0)
				step = Math.Min(step, 0.05);
			else if (m_fCurrentFlashlightBrightness < 3.0)
				step = Math.Min(step, 0.1);
			
			if (!increase)
				step = -step;
			
			// Use effective min/max from active flashlight sources
			float minBright = GetEffectiveMinBrightness(true);
			float maxBright = GetEffectiveMaxBrightness(true);
			m_fCurrentFlashlightBrightness = Math.Clamp(m_fCurrentFlashlightBrightness + step, minBright, maxBright);
			ApplyFlashlightBrightness();
			
			// Sync over network
			if (Replication.IsRunning())
			{
				ACM_NVL_NetworkComponent netComp = FindNetworkComponent();
				if (netComp)
					netComp.AskSetFlashlightBrightness(this, m_fCurrentFlashlightBrightness);
			}
		}

		// Adjust IR brightness if on
		if (m_bIROn)
		{
			// Get step from active sources
			float step = GetEffectiveBrightnessStep(false);
			
			// Finer control at low brightness
			if (m_fCurrentIRBrightness < 0.5)
				step = Math.Min(step, 0.02);
			else if (m_fCurrentIRBrightness < 1.0)
				step = Math.Min(step, 0.05);
			
			if (!increase)
				step = -step;
			
			// Use effective min/max from active IR sources
			float minBright = GetEffectiveMinBrightness(false);
			float maxBright = GetEffectiveMaxBrightness(false);
			m_fCurrentIRBrightness = Math.Clamp(m_fCurrentIRBrightness + step, minBright, maxBright);
			ApplyIRBrightness();
			
			// Sync over network
			if (Replication.IsRunning())
			{
				ACM_NVL_NetworkComponent netComp = FindNetworkComponent();
				if (netComp)
					netComp.AskSetIRBrightness(this, m_fCurrentIRBrightness);
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	// Beam cone angle adjustment (widen = true increases angle, false narrows)
	// Mirrors AdjustBrightness flow exactly: local apply first, then network sync.
	//------------------------------------------------------------------------------------------------
	protected void AdjustConeAngle(bool widen)
	{
		// Gate 1: not yet FULLY_READY
		if (!m_bReadinessGateOpen)
			return;
		// Gate 2: not the local owning player (cached char avoids re-walking the inventory chain)
		if (!IsLocalPlayerOwnerCached())
			return;
		// Gate 3 (weapon-only): replication must be running - no local-only fallback for weapons.
		if (IsOnWeapon() && !Replication.IsRunning())
			return;

		// All gates passed: apply locally then send RPC.
		int step = GetEffectiveConeStep();
		if (!widen)
			step = -step;
		SetConeAngle(Math.ClampInt(m_iCurrentConeAngle + step, GetEffectiveMinConeAngle(), GetEffectiveMaxConeAngle()));

		// Non-weapon equipment in singleplayer: local apply is sufficient, no RPC needed.
		// Weapon components already returned above if replication was not running.
		if (!Replication.IsRunning())
			return;

		string id = GetResolvedNetworkIdentifier();
		ACM_NVL_NetworkComponent netComp = FindNetworkComponent();

		if (!netComp)
			return;

		if (id.IsEmpty())
			return;

		netComp.AskSetBeamAngle(this, m_iCurrentConeAngle);
	}
	
	void SetConeAngle(int angle)
	{
		m_iCurrentConeAngle = Math.ClampInt(angle, GetEffectiveMinConeAngle(), GetEffectiveMaxConeAngle());
		ApplyConeAngle();
		if (m_bFlashlightOn)
			ApplyFlashlightBrightness();
	}
	
	void SetConeAngleFromNetwork(int angle)
	{
		m_bSuppressNetworkSync = true;
		SetConeAngle(angle);
		m_bSuppressNetworkSync = false;
	}
	
	int GetConeAngle() { return m_iCurrentConeAngle; }
	
	// Returns true if at least one flashlight source supports cone control.
	// Checks spawned light entities first; falls back to source configuration so that
	// input is never blocked by a timing race where lights have not yet been spawned.
	bool SupportsConeControl()
	{
		// Primary: check spawned LightEntity objects (best-case with actual entities)
		if (m_mLights && !m_mLights.IsEmpty())
		{
			for (int i = 0; i < m_mLights.Count(); i++)
			{
				ACM_NVL_LightSource src = m_mLights.GetKey(i);
				IEntity entity = m_mLights.GetElement(i);
				if (!src || !src.IsFlashlight())
					continue;
				if (!LightEntity.Cast(entity))
					continue;
				return true;
			}
		}

		// Fallback: a configured flashlight source means cone control is intended even
		// before lights are spawned (e.g. during the 100 ms DeferredInit delay, or after
		// m_mLights is temporarily cleared during a weapon re-equip cycle).
		if (m_aSources)
		{
			foreach (ACM_NVL_LightSource src : m_aSources)
			{
				if (src && src.IsFlashlight())
					return true;
			}
		}

		return false;
	}
	
	protected void ApplyConeAngle()
	{
		if (!m_mLights)
			return;
		
		for (int i = 0; i < m_mLights.Count(); i++)
		{
			ACM_NVL_LightSource src = m_mLights.GetKey(i);
			IEntity entity = m_mLights.GetElement(i);
			
			if (!src || !src.IsFlashlight())
				continue;
			
			LightEntity light = LightEntity.Cast(entity);
			if (!light)
				continue;
			
			int clamped = Math.ClampInt(m_iCurrentConeAngle, src.GetMinConeAngle(), src.GetMaxConeAngle());
			
			// Toggle off/on to force the engine to refresh the cone angle on an already-enabled spotlight.
			// SetConeAngle alone does not visually update a live light; a disable/enable cycle is required.
			bool wasEnabled = light.IsEnabled();
			if (wasEnabled)
				light.SetEnabled(false);
			light.SetConeAngle(clamped);
			if (wasEnabled)
				light.SetEnabled(true);
		}
	}
	
	protected int GetEffectiveDefaultConeAngle()
	{
		if (!m_aSources)
			return 25;
		
		foreach (ACM_NVL_LightSource src : m_aSources)
		{
			if (src && src.IsFlashlight())
				return src.GetDefaultConeAngle();
		}
		return 25;
	}
	
	protected int GetEffectiveMinConeAngle()
	{
		if (!m_aSources)
			return 5;
		
		int minAngle = int.MAX;
		bool found = false;
		foreach (ACM_NVL_LightSource src : m_aSources)
		{
			if (src && src.IsFlashlight())
			{
				if (src.GetMinConeAngle() < minAngle)
					minAngle = src.GetMinConeAngle();
				found = true;
			}
		}
		if (found)
			return minAngle;
		return 5;
	}
	
	protected int GetEffectiveMaxConeAngle()
	{
		if (!m_aSources)
			return 45;
		
		int maxAngle = 0;
		bool found = false;
		foreach (ACM_NVL_LightSource src : m_aSources)
		{
			if (src && src.IsFlashlight())
			{
				if (src.GetMaxConeAngle() > maxAngle)
					maxAngle = src.GetMaxConeAngle();
				found = true;
			}
		}
		if (found)
			return maxAngle;
		return 45;
	}
	
	protected int GetEffectiveConeStep()
	{
		if (!m_aSources)
			return 5;
		
		foreach (ACM_NVL_LightSource src : m_aSources)
		{
			if (src && src.IsFlashlight())
				return src.GetConeAngleStep();
		}
		return 5;
	}

	// Returns a multiplier in [CONE_BRIGHTNESS_MIN_SCALE..1.0] for cone-controlled flashlights.
	protected float GetConeBrightnessScaleForSource(ACM_NVL_LightSource src)
	{
		if (!src || !src.IsFlashlight())
			return 1.0;

		int minCone = src.GetMinConeAngle();
		int maxCone = src.GetMaxConeAngle();
		if (maxCone <= minCone)
			return 1.0;

		int clampedCone = Math.ClampInt(m_iCurrentConeAngle, minCone, maxCone);
		float t = (clampedCone - minCone) * 1.0 / (maxCone - minCone);
		return 1.0 - (t * (1.0 - CONE_BRIGHTNESS_MIN_SCALE));
	}
	
	protected void ApplyFlashlightBrightness()
	{
		if (!m_mLights)
			return;
		
		vector color = GetCurrentColor();
		
		for (int i = 0; i < m_mLights.Count(); i++)
		{
			ACM_NVL_LightSource src = m_mLights.GetKey(i);
			IEntity entity = m_mLights.GetElement(i);
			
			if (!src || !src.IsFlashlight())
				continue;
			
			LightEntity light = LightEntity.Cast(entity);
			if (!light || !light.IsEnabled())
				continue;
			
			// Clamp brightness to this source's min/max before applying
			float minBright = GetSourceMinBrightness(src);
			float maxBright = GetSourceMaxBrightness(src);
			float clampedBrightness = Math.Clamp(m_fCurrentFlashlightBrightness, minBright, maxBright);
			clampedBrightness = clampedBrightness * GetConeBrightnessScaleForSource(src);
			
			light.SetColor(Color.FromVector(color), clampedBrightness);
			
			if (m_EmissiveMat)
			{
				m_EmissiveMat.SetEmissiveMultiplier(clampedBrightness * 2);
			}
		}
	}
	
	protected void ApplyIRBrightness()
	{
		if (!m_mLights)
			return;
		
		vector color = GetCurrentColor();
		
		for (int i = 0; i < m_mLights.Count(); i++)
		{
			ACM_NVL_LightSource src = m_mLights.GetKey(i);
			IEntity entity = m_mLights.GetElement(i);
			
			if (!src || !src.IsIR())
				continue;
			
			// Skip ACM_IRLaser - it handles its own appearance
			if (ACM_IRLaser.Cast(entity))
				continue;
			
			LightEntity light = LightEntity.Cast(entity);
			if (!light || !light.IsEnabled())
				continue;
			
			// Clamp brightness to this source's min/max before applying
			float minBright = GetSourceMinBrightness(src);
			float maxBright = GetSourceMaxBrightness(src);
			float clampedBrightness = Math.Clamp(m_fCurrentIRBrightness, minBright, maxBright);
			
			light.SetColor(Color.FromVector(color), clampedBrightness);
			
			if (m_EmissiveMat && m_bIRFilterRemoved)
			{
				m_EmissiveMat.SetEmissiveMultiplier(clampedBrightness * 2);
			}
		}
	}
	
	//------------------------------------------------------------------------------------------------
	// Get initial brightness from sources (per-source defaults) or component default
	//------------------------------------------------------------------------------------------------
	protected float GetInitialFlashlightBrightness()
	{
		if (!m_aSources || m_aSources.IsEmpty())
			return 6.0; // Default fallback
		
		// Find first flashlight source and use its default
		foreach (ACM_NVL_LightSource src : m_aSources)
		{
			if (src && src.IsFlashlight())
				return src.GetDefaultBrightness();
		}
		
		return 6.0; // Default fallback
	}
	
	protected float GetInitialIRBrightness()
	{
		if (!m_aSources || m_aSources.IsEmpty())
			return 1.0; // Default fallback
		
		// Find first IR source and use its default
		foreach (ACM_NVL_LightSource src : m_aSources)
		{
			if (src && src.IsIR())
				return src.GetDefaultBrightness();
		}
		
		return 1.0; // Default fallback
	}
	
	//------------------------------------------------------------------------------------------------
	// Get min/max brightness for a specific source
	//------------------------------------------------------------------------------------------------
	protected float GetSourceMinBrightness(ACM_NVL_LightSource src)
	{
		if (!src)
			return 0.1; // Default fallback
		return src.GetMinBrightness();
	}
	
	protected float GetSourceMaxBrightness(ACM_NVL_LightSource src)
	{
		if (!src)
			return 6.0; // Default fallback

		float sourceMax = src.GetMaxBrightness();
		if (src.IsFlashlight())
			return Math.Min(sourceMax, 6.0);

		return sourceMax;
	}
	
	protected float GetSourceBrightnessStep(ACM_NVL_LightSource src)
	{
		if (!src)
			return 0.2; // Default fallback
		return src.GetBrightnessStep();
	}
	
	//------------------------------------------------------------------------------------------------
	// Get effective min/max for current active lights (for brightness adjustment)
	//------------------------------------------------------------------------------------------------
	protected float GetEffectiveMinBrightness(bool isFlashlight)
	{
		if (!m_mLights || m_mLights.IsEmpty())
			return 0.1; // Default fallback
		
		float minBrightness = 100.0; // Start high, find actual min
		bool foundAny = false;
		
		for (int i = 0; i < m_mLights.Count(); i++)
		{
			ACM_NVL_LightSource src = m_mLights.GetKey(i);
			if (!src)
				continue;
			
			if ((isFlashlight && src.IsFlashlight()) || (!isFlashlight && src.IsIR()))
			{
				float sourceMin = GetSourceMinBrightness(src);
				if (sourceMin < minBrightness)
					minBrightness = sourceMin;
				foundAny = true;
			}
		}
		
		if (foundAny)
			return minBrightness;
		return 0.1; // Default fallback
	}
	
	protected float GetEffectiveMaxBrightness(bool isFlashlight)
	{
		if (!m_mLights || m_mLights.IsEmpty())
			return 6.0; // Default fallback
		
		float maxBrightness = 0.0; // Start low, find actual max
		bool foundAny = false;
		
		for (int i = 0; i < m_mLights.Count(); i++)
		{
			ACM_NVL_LightSource src = m_mLights.GetKey(i);
			if (!src)
				continue;
			
			if ((isFlashlight && src.IsFlashlight()) || (!isFlashlight && src.IsIR()))
			{
				float sourceMax = GetSourceMaxBrightness(src);
				if (sourceMax > maxBrightness)
					maxBrightness = sourceMax;
				foundAny = true;
			}
		}
		
		if (foundAny)
			return maxBrightness;
		return 6.0; // Default fallback
	}
	
	//------------------------------------------------------------------------------------------------
	// Get effective brightness step for current active lights (for brightness adjustment)
	//------------------------------------------------------------------------------------------------
	protected float GetEffectiveBrightnessStep(bool isFlashlight)
	{
		if (!m_mLights || m_mLights.IsEmpty())
			return 0.2; // Default fallback
		
		// Use the first active light's step, or average if multiple
		for (int i = 0; i < m_mLights.Count(); i++)
		{
			ACM_NVL_LightSource src = m_mLights.GetKey(i);
			if (!src)
				continue;
			
			if ((isFlashlight && src.IsFlashlight()) || (!isFlashlight && src.IsIR()))
			{
				return GetSourceBrightnessStep(src);
			}
		}
		
		return 0.2; // Default fallback
	}
	
	// Public accessors for brightness
	float GetFlashlightBrightness() { return m_fCurrentFlashlightBrightness; }
	float GetIRBrightness() { return m_fCurrentIRBrightness; }
	
	void SetFlashlightBrightness(float brightness)
	{
		// Clamp to effective min/max of all active flashlight sources
		float minBright = GetEffectiveMinBrightness(true);
		float maxBright = GetEffectiveMaxBrightness(true);
		m_fCurrentFlashlightBrightness = Math.Clamp(brightness, minBright, maxBright);
		if (m_bFlashlightOn)
			ApplyFlashlightBrightness();
	}
	
	void SetFlashlightBrightnessFromNetwork(float brightness)
	{
		m_bSuppressNetworkSync = true;
		SetFlashlightBrightness(brightness);
		m_bSuppressNetworkSync = false;
	}
	
	void SetIRBrightness(float brightness)
	{
		// Clamp to effective min/max of all active IR sources
		float minBright = GetEffectiveMinBrightness(false);
		float maxBright = GetEffectiveMaxBrightness(false);
		m_fCurrentIRBrightness = Math.Clamp(brightness, minBright, maxBright);
		if (m_bIROn)
			ApplyIRBrightness();
	}
	
	void SetIRBrightnessFromNetwork(float brightness)
	{
		m_bSuppressNetworkSync = true;
		SetIRBrightness(brightness);
		m_bSuppressNetworkSync = false;
	}

	void RequestToggleFlashlight()
	{
		ACM_NVL_NetworkComponent netComp = FindNetworkComponent();
		if (!m_bReadinessGateOpen)
			return;
		if (!IsLocalPlayerOwnerCached())
			return;
		if (netComp)
			netComp.AskToggleFlashlight(this);
		else
			ApplyFlashlightState(!m_bFlashlightOn);
	}

	void RequestToggleIR()
	{
		ACM_NVL_NetworkComponent netComp = FindNetworkComponent();
		if (!m_bReadinessGateOpen)
			return;
		if (!IsLocalPlayerOwnerCached())
			return;
		if (netComp)
			netComp.AskToggleIR(this);
		else
			ApplyIRState(!m_bIROn);
	}

	void ApplyFlashlightState(bool state)
	{
		m_bFlashlightOn = state;

		if (!m_bLightsSpawned && m_aSources && !m_aSources.IsEmpty())
			SpawnLights();
		
		if (!m_mLights)
			return;
		
		vector currentColor = GetCurrentColor();
		
		if (state)
			ApplyConeAngle();

		for (int i = 0; i < m_mLights.Count(); i++)
		{
			ACM_NVL_LightSource src = m_mLights.GetKey(i);
			IEntity entity = m_mLights.GetElement(i);

			if (!src || !src.IsFlashlight())
				continue;

			if (!entity)
				continue;

			if (!m_Owner)
				continue;
			// If entity lost its parent (AddChild failed on remote client), attempt re-parenting
			if (entity.GetParent() != m_Owner)
			{
				PointInfo srcOffset = src.GetOffset();
				TNodeId pivot = -1;
				if (srcOffset)
					pivot = srcOffset.GetNodeId();
				m_Owner.AddChild(entity, pivot, EAddChildFlags.AUTO_TRANSFORM);
				if (srcOffset && entity.GetParent() == m_Owner)
				{
					vector xf[4];
					srcOffset.GetLocalTransform(xf);
					entity.SetLocalTransform(xf);
				}
				if (entity.GetParent() != m_Owner)
					continue;
			}

			LightEntity light = LightEntity.Cast(entity);
			if (!light)
				continue;

			if (state)
			{
				light.SetEnabled(true);
				entity.SetFlags(EntityFlags.VISIBLE);
				int clampedCone = Math.ClampInt(m_iCurrentConeAngle, src.GetMinConeAngle(), src.GetMaxConeAngle());
				light.SetConeAngle(clampedCone);
				
				// Apply current color and brightness to light (clamped to source's min/max)
				float minBright = GetSourceMinBrightness(src);
				float maxBright = GetSourceMaxBrightness(src);
				float clampedBrightness = Math.Clamp(m_fCurrentFlashlightBrightness, minBright, maxBright);
				clampedBrightness = clampedBrightness * GetConeBrightnessScaleForSource(src);
				light.SetColor(Color.FromVector(currentColor), clampedBrightness);
			}
			else
			{
				entity.ClearFlags(EntityFlags.VISIBLE);
				light.SetEnabled(false);
			}

			if (m_EmissiveMat)
			{
				if (state)
				{
					// Use clamped brightness for emissive material too
					float minBright = GetSourceMinBrightness(src);
					float maxBright = GetSourceMaxBrightness(src);
					float clampedBrightness = Math.Clamp(m_fCurrentFlashlightBrightness, minBright, maxBright);
					clampedBrightness = clampedBrightness * GetConeBrightnessScaleForSource(src);
					m_EmissiveMat.SetEmissiveMultiplier(clampedBrightness * 2);
					Color c = Color.FromVector(currentColor);
					m_EmissiveMat.SetEmissiveColor(c.PackToInt());
				}
				else
				{
					m_EmissiveMat.SetEmissiveMultiplier(0);
				}
			}

			string soundEvent = src.GetSoundEvent();
			if (m_SoundComp && !soundEvent.IsEmpty())
				m_SoundComp.SoundEvent(soundEvent);
		}
	}

	void ApplyIRState(bool state)
	{
		m_bIROn = state;
		
		// Refresh NVG state from IR system before applying
		ACM_IRSystem irSys = ACM_IRSystem.GetInstance();
		if (irSys)
			m_bNVGActive = irSys.IsIRVisible();
		
		// Removed verbose logging - only log warnings/errors

		if (!m_bLightsSpawned && m_aSources && !m_aSources.IsEmpty())
			SpawnLights();
		
		if (!m_mLights)
		{
			return;
		}
		
		// Removed verbose logging - only log warnings/errors
		
		vector currentColor = GetCurrentColor();

		for (int i = 0; i < m_mLights.Count(); i++)
		{
			ACM_NVL_LightSource src = m_mLights.GetKey(i);
			IEntity entity = m_mLights.GetElement(i);
			
			// Removed verbose logging - only log warnings/errors

			if (!src || !src.IsIR())
			{
				// Removed verbose logging - only log warnings/errors
				continue;
			}

			if (!entity)
			{
				continue;
			}

			if (!m_Owner)
				continue;
			// If entity lost its parent (AddChild failed on remote client), attempt re-parenting
			if (entity.GetParent() != m_Owner)
			{
				PointInfo srcOffset = src.GetOffset();
				TNodeId pivot = -1;
				if (srcOffset)
					pivot = srcOffset.GetNodeId();
				m_Owner.AddChild(entity, pivot, EAddChildFlags.AUTO_TRANSFORM);
				if (srcOffset && entity.GetParent() == m_Owner)
				{
					vector xf[4];
					srcOffset.GetLocalTransform(xf);
					entity.SetLocalTransform(xf);
				}
				if (entity.GetParent() != m_Owner)
				{
					continue;
				}
			}

			// Check if this is a ACM_IRLaser entity (has its own visibility system)
			ACM_IRLaser irLaser = ACM_IRLaser.Cast(entity);
			if (irLaser)
			{
				// Removed verbose logging - only log warnings/errors
				irLaser.SetIREnabledWithNVGState(state, m_bNVGActive);
				
				string soundEvent = src.GetSoundEvent();
				if (m_SoundComp && !soundEvent.IsEmpty())
					m_SoundComp.SoundEvent(soundEvent);
				
				continue;
			}
			
			// Regular LightEntity
			LightEntity light = LightEntity.Cast(entity);
			if (!light)
			{
				continue;
			}
			
			// Removed verbose logging - only log warnings/errors

			// IR visible if: ON and (NVG active OR cover removed)
			bool shouldBeVisible = state && (m_bNVGActive || m_bIRFilterRemoved);
			
			// Removed verbose logging - only log warnings/errors

			if (shouldBeVisible)
			{
				light.SetEnabled(true);
				entity.SetFlags(EntityFlags.VISIBLE);
				// Removed verbose logging - only log warnings/errors
				
				// Clamp brightness to this source's min/max before applying
				float minBright = GetSourceMinBrightness(src);
				float maxBright = GetSourceMaxBrightness(src);
				float clampedBrightness = Math.Clamp(m_fCurrentIRBrightness, minBright, maxBright);
				light.SetColor(Color.FromVector(currentColor), clampedBrightness);
				
				if (m_bIRFilterRemoved && m_EmissiveMat)
				{
					m_EmissiveMat.SetEmissiveMultiplier(clampedBrightness * 2);
					Color c = Color.FromVector(currentColor);
					m_EmissiveMat.SetEmissiveColor(c.PackToInt());
				}
			}
			else
			{
				entity.ClearFlags(EntityFlags.VISIBLE);
				light.SetEnabled(false);
				// Removed verbose logging - only log warnings/errors
				
				if (m_EmissiveMat)
					m_EmissiveMat.SetEmissiveMultiplier(0);
			}

			string soundEvent = src.GetSoundEvent();
			if (m_SoundComp && !soundEvent.IsEmpty())
				m_SoundComp.SoundEvent(soundEvent);
		}
	}
	
	void OnNVGStateChanged(bool nvgActive)
	{
		m_bNVGActive = nvgActive;
		
		// Update all IR lights including ACM_IRLaser entities
		if (m_bIROn)
		{
			// Directly update ACM_IRLaser entities with the new NVG state
			if (m_mLights)
			{
				for (int i = 0; i < m_mLights.Count(); i++)
				{
					ACM_NVL_LightSource src = m_mLights.GetKey(i);
					IEntity entity = m_mLights.GetElement(i);
					
					if (!src || !src.IsIR() || !entity)
						continue;
					
					ACM_IRLaser irLaser = ACM_IRLaser.Cast(entity);
					if (irLaser)
					{
						irLaser.OnNVGStateChanged(nvgActive);
					}
				}
			}
			
			// Update regular IR lights
			UpdateIRVisibility();
		}
	}
	
	protected void UpdateIRVisibility()
	{
		if (!m_bLightsSpawned)
			return;
		
		if (!m_mLights)
			return;
		
		vector currentColor = GetCurrentColor();

		for (int i = 0; i < m_mLights.Count(); i++)
		{
			ACM_NVL_LightSource src = m_mLights.GetKey(i);
			IEntity entity = m_mLights.GetElement(i);

			if (!src || !src.IsIR())
				continue;

			if (!entity)
				continue;

			if (!m_Owner || entity.GetParent() != m_Owner)
				continue;

			// Skip ACM_IRLaser - it handles its own visibility
			if (ACM_IRLaser.Cast(entity))
				continue;

			// Regular LightEntity
			LightEntity light = LightEntity.Cast(entity);
			if (!light)
				continue;

			// IR visible if: ON and (NVG active OR cover removed)
			bool shouldBeVisible = m_bIROn && (m_bNVGActive || m_bIRFilterRemoved);

			if (shouldBeVisible)
			{
				light.SetEnabled(true);
				entity.SetFlags(EntityFlags.VISIBLE);
				
				// Clamp brightness to this source's min/max before applying
				float minBright = GetSourceMinBrightness(src);
				float maxBright = GetSourceMaxBrightness(src);
				float clampedBrightness = Math.Clamp(m_fCurrentIRBrightness, minBright, maxBright);
				light.SetColor(Color.FromVector(currentColor), clampedBrightness);
				
				if (m_bIRFilterRemoved && m_EmissiveMat)
				{
					m_EmissiveMat.SetEmissiveMultiplier(clampedBrightness * 2);
					Color c = Color.FromVector(currentColor);
					m_EmissiveMat.SetEmissiveColor(c.PackToInt());
				}
			}
			else
			{
				entity.ClearFlags(EntityFlags.VISIBLE);
				light.SetEnabled(false);
				
				if (m_EmissiveMat)
					m_EmissiveMat.SetEmissiveMultiplier(0);
			}
		}
	}

	bool IsFlashlightOn() { return m_bFlashlightOn; }
	bool IsIROn() { return m_bIROn; }
	bool IsNVGActive() { return m_bNVGActive; }
	bool IsIRFilterRemoved() { return m_bIRFilterRemoved; }
	string GetLightIdentifier() { return m_sLightIdentifier; }

	//------------------------------------------------------------------------------------------------
	// Returns the stable identifier under which this component is currently registered with
	// ACM_NVL_NetworkComponent.  Set by CheckReadiness() from GetResolvedNetworkIdentifier() at
	// registration time and never changes until the component re-registers.  Callers inside the
	// NetworkComponent should prefer this over calling GetResolvedNetworkIdentifier() directly;
	// that avoids re-walking the RplId chain on every RPC call after FULLY_READY.
	// Falls back to GetResolvedNetworkIdentifier() before registration completes.
	//------------------------------------------------------------------------------------------------
	string GetRegisteredIdentifier()
	{
		if (!m_sRegisteredNetworkId.IsEmpty())
			return m_sRegisteredNetworkId;
		return GetResolvedNetworkIdentifier();
	}
	
	//------------------------------------------------------------------------------------------------
	// GetResolvedNetworkIdentifier
	//
	// Returns a stable, unique string key that matches on ALL machines (owning client, dedicated
	// server, remote clients) for the same component instance.
	//
	// Priority order:
	//   1. Authored m_sLightIdentifier attribute - always deterministic; REQUIRED when a single
	//      entity carries more than one ACM_NVL_LightsComponent of the same class.
	//   2. Owner entity has its own RplComponent - directly use its replicated ID.
	//   3. Inventory/attachment slot chain - walk up InventoryItemComponent.GetParentSlot()
	//      until an ancestor with a positive RplId is found.  The chain of entity class names
	//      between the owner and the replicated ancestor forms a collision-resistant suffix.
	//      Works for weapon attachments because the weapon entity is a replicated entity.
	//   4. Scene parent chain - walk GetParent() for entities AddChild'd to a replicated parent.
	//   5. Entity name - only if explicitly named in the editor.
	//   6. Source-config signature - last resort; collision-prone when two lights of the same
	//      class and source layout exist on the same character.  Set m_sLightIdentifier to avoid.
	//------------------------------------------------------------------------------------------------
	string GetResolvedNetworkIdentifier()
	{
		string compType = Type().ToString();
		if (compType.IsEmpty())
			compType = "ACM_NVL_LightsComponent";

		// 1. Authored identifier (always wins)
		if (!m_sLightIdentifier.IsEmpty())
			return string.Format("%1:id_%2", compType, m_sLightIdentifier);

		if (!m_Owner)
			return compType + ":noowner";

		// 2. Owner is directly replicated
		int rplId = Replication.FindId(m_Owner);
		if (rplId >= 0)
			return string.Format("%1:rpl_%2", compType, rplId);

		// 3. Walk inventory parent chain to find a replicated ancestor.
		//    Accumulate a chain of entity class-name segments as a collision-resistant suffix.
		//    A weapon-mounted flashlight reaches the weapon entity (which IS replicated) in 1-2 steps.
		{
			IEntity walkEnt = m_Owner;
			string chainSig = "";
			for (int depth = 0; depth < 8; depth++)
			{
				InventoryItemComponent ic = InventoryItemComponent.Cast(walkEnt.FindComponent(InventoryItemComponent));
				if (!ic)
					break;
				InventoryStorageSlot parentSlot = ic.GetParentSlot();
				if (!parentSlot)
					break;
				IEntity slotOwner = parentSlot.GetOwner();
				if (!slotOwner)
					break;

				// Append this entity's class name to the chain (closest-first order)
				string typeSeg = walkEnt.Type().ToString();
				if (chainSig.IsEmpty())
					chainSig = typeSeg;
				else
					chainSig = typeSeg + "/" + chainSig;

				int ancestorRplId = Replication.FindId(slotOwner);
				if (ancestorRplId >= 0)
					return string.Format("%1:anc_%2_%3", compType, ancestorRplId, chainSig);

				walkEnt = slotOwner;
			}
		}

		// 4. Scene parent chain - for entities directly AddChild'd to a replicated parent
		{
			IEntity parent = m_Owner.GetParent();
			while (parent)
			{
				int parentRplId = Replication.FindId(parent);
				if (parentRplId >= 0)
				{
					string ownerType = m_Owner.Type().ToString();
					return string.Format("%1:scn_%2_%3", compType, parentRplId, ownerType);
				}
				parent = parent.GetParent();
			}
		}

		// 5. Entity name - only useful when the author has given the entity a unique name
		string ownerName = m_Owner.GetName();
		if (!ownerName.IsEmpty())
			return string.Format("%1:name_%2", compType, ownerName);

		// 6. Source-configuration signature (collision-prone - set m_sLightIdentifier to avoid)
		int sCount = 0, fCount = 0, irCount = 0;
		if (m_aSources)
		{
			sCount = m_aSources.Count();
			foreach (ACM_NVL_LightSource src : m_aSources)
			{
				if (!src) continue;
				if (src.IsFlashlight()) fCount++;
				if (src.IsIR()) irCount++;
			}
		}
		return string.Format("%1:cfg_%2s_%3f_%4ir", compType, sCount, fCount, irCount);
	}
	
	//------------------------------------------------------------------------------------------------
	// Light color methods
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
	
	void SetLightColor(int colorIndex)
	{
		if (colorIndex < 0 || colorIndex >= s_aLightColors.Count())
			colorIndex = 0;
		
		m_iCurrentColorIndex = colorIndex;
		
		// Apply color if flashlight is on
		if (m_bFlashlightOn)
			ApplyFlashlightColor();
		
		// Apply color to IR if IR channel is on (works for both IR-only and cover-removed modes)
		if (m_bIROn)
			ApplyIRColor();
		
		// Sync over network if we're the local owner and not suppressed
		if (!m_bSuppressNetworkSync && IsLocalPlayerOwner() && Replication.IsRunning())
		{
			ACM_NVL_NetworkComponent netComp = FindNetworkComponent();
			if (netComp)
				netComp.AskSetLightColor(this, colorIndex);
		}
	}
	
	// Called from network - sets color without sending another RPC
	void SetLightColorFromNetwork(int colorIndex)
	{
		m_bSuppressNetworkSync = true;
		SetLightColor(colorIndex);
		m_bSuppressNetworkSync = false;
	}
	
	void CycleLightColor()
	{
		m_iCurrentColorIndex = (m_iCurrentColorIndex + 1) % s_aLightColors.Count();
		
		// Removed verbose logging - only log warnings/errors
		
		// Apply color if flashlight is on
		if (m_bFlashlightOn)
			ApplyFlashlightColor();
		
		// Apply color to IR if on and cover removed
		if (m_bIROn && m_bIRFilterRemoved)
			ApplyIRColor();
	}
	
	protected void ApplyFlashlightColor()
	{
		if (!m_mLights)
			return;
		
		vector color = GetCurrentColor();
		
		for (int i = 0; i < m_mLights.Count(); i++)
		{
			ACM_NVL_LightSource src = m_mLights.GetKey(i);
			IEntity entity = m_mLights.GetElement(i);
			
			if (!src || !src.IsFlashlight())
				continue;
			
			LightEntity light = LightEntity.Cast(entity);
			if (!light)
				continue;
			
			// Clamp brightness to this source's min/max before applying
			float minBright = GetSourceMinBrightness(src);
			float maxBright = GetSourceMaxBrightness(src);
			float clampedBrightness = Math.Clamp(m_fCurrentFlashlightBrightness, minBright, maxBright);
			clampedBrightness = clampedBrightness * GetConeBrightnessScaleForSource(src);
			light.SetColor(Color.FromVector(color), clampedBrightness);
			
			if (m_EmissiveMat)
			{
				Color c = Color.FromVector(color);
				m_EmissiveMat.SetEmissiveColor(c.PackToInt());
			}
		}
	}
	
	// Apply color to IR lights (when cover is removed)
	protected void ApplyIRColor()
	{
		if (!m_mLights)
			return;
		
		vector color = GetCurrentColor();
		
		for (int i = 0; i < m_mLights.Count(); i++)
		{
			ACM_NVL_LightSource src = m_mLights.GetKey(i);
			IEntity entity = m_mLights.GetElement(i);
			
			if (!src || !src.IsIR())
				continue;
			
			// Handle ACM_IRLaser color change
			ACM_IRLaser irLaser = ACM_IRLaser.Cast(entity);
			if (irLaser)
			{
				irLaser.SetIRColor(color);
				irLaser.SetVisibleColor(color);
				continue;
			}
			
			LightEntity light = LightEntity.Cast(entity);
			if (!light)
				continue;
			
			// Clamp brightness to this source's min/max before applying
			float minBright = GetSourceMinBrightness(src);
			float maxBright = GetSourceMaxBrightness(src);
			float clampedBrightness = Math.Clamp(m_fCurrentIRBrightness, minBright, maxBright);
			light.SetColor(Color.FromVector(color), clampedBrightness);
			
			if (m_EmissiveMat)
			{
				Color c = Color.FromVector(color);
				m_EmissiveMat.SetEmissiveColor(c.PackToInt());
			}
		}
	}
	
	//------------------------------------------------------------------------------------------------
	// Toggle IR filter - when removed, IR is visible without NVG
	//------------------------------------------------------------------------------------------------
	void SetIRFilterRemoved(bool removed)
	{
		m_bIRFilterRemoved = removed;
		
		// Removed verbose logging - only log warnings/errors
		
		// Update ACM_IRLaser entities with filter state
		if (m_mLights)
		{
			for (int i = 0; i < m_mLights.Count(); i++)
			{
				ACM_NVL_LightSource src = m_mLights.GetKey(i);
				IEntity entity = m_mLights.GetElement(i);
				
				if (!src || !src.IsIR() || !entity)
					continue;
				
				ACM_IRLaser irLaser = ACM_IRLaser.Cast(entity);
				if (irLaser)
				{
					irLaser.SetIRFilterRemoved(removed);
				}
			}
		}
		
		// Update IR visibility immediately if IR is on
		if (m_bIROn)
			UpdateIRVisibility();
		
		// Sync over network if we're the local owner and not suppressed
		if (!m_bSuppressNetworkSync && IsLocalPlayerOwner() && Replication.IsRunning())
		{
			ACM_NVL_NetworkComponent netComp = FindNetworkComponent();
			if (netComp)
				netComp.AskSetIRFilterRemoved(this, removed);
		}
	}
	
	void SetIRFilterRemovedFromNetwork(bool removed)
	{
		m_bSuppressNetworkSync = true;
		SetIRFilterRemoved(removed);
		m_bSuppressNetworkSync = false;
	}
	
	void ToggleIRFilter()
	{
		SetIRFilterRemoved(!m_bIRFilterRemoved);
	}
	
	//------------------------------------------------------------------------------------------------
	// Check if this component has a specific light type configured
	//------------------------------------------------------------------------------------------------
	bool HasLightType(ACM_NVL_LightType lightType)
	{
		if (!m_aSources)
			return false;
		
		foreach (ACM_NVL_LightSource src : m_aSources)
		{
			if (!src)
				continue;
			
			if (lightType == ACM_NVL_LightType.FLASHLIGHT && src.IsFlashlight())
				return true;
			
			if (lightType == ACM_NVL_LightType.IRLIGHT && src.IsIR())
				return true;
		}
		
		return false;
	}

	//------------------------------------------------------------------------------------------------
	// Reapply all current stored state to local light entities.
	// Call after SpawnLights(), weapon re-equip, reparent, or network state delivery.
	// Does NOT trigger outbound network calls - only updates local visuals.
	// Deliberate apply order:
	//   1. IR filter state (must propagate to ACM_IRLaser before IR is turned on)
	//   2. Cone angle (so flashlight enables with the correct beam width)
	//   3. Flashlight on/off (also applies color + brightness via ApplyFlashlightState)
	//   4. IR on/off
	//------------------------------------------------------------------------------------------------
	protected void ApplyAllCurrentState()
	{
		// Ensure light entities exist
		if (!m_bLightsSpawned && m_aSources && !m_aSources.IsEmpty())
			SpawnLights();

		if (!m_mLights)
			return;

		// 1. Propagate IR filter / cover state to ACM_IRLaser child entities
		SetIRFilterRemovedFromNetwork(m_bIRFilterRemoved);

		// 2. Apply cone angle so lights start with the right beam width
		if (SupportsConeControl())
			ApplyConeAngle();

		// 3. Apply flashlight state (covers both on->off and off->on; also applies color and brightness)
		ApplyFlashlightState(m_bFlashlightOn);

		// 4. Apply IR state
		ApplyIRState(m_bIROn);
	}

	// NOTE: RplSave / RplLoad have been intentionally removed from this component.
	// All light state (flashlight on/off, IR, brightness, beam angle, color, filter) is
	// owned and replicated exclusively by ACM_NVL_NetworkComponent on the character entity.
	// Duplicating replication here caused race conditions and inconsistent JIP state.
	// JIP is handled by NetworkComponent.RplSave/RplLoad + ApplyAllStatesFromJIP.

	override void OnDelete(IEntity owner)
	{
		// Unregister from NetworkComponent registry using the tracked ID (not recomputed,
		// since GetResolvedNetworkIdentifier() could produce a different value during teardown)
		if (m_mNetworkComponentRef && !m_sRegisteredNetworkId.IsEmpty())
		{
			m_mNetworkComponentRef.UnregisterLightsComponent(m_sRegisteredNetworkId);
			m_mNetworkComponentRef = null;
			m_sRegisteredNetworkId = "";
		}

		if (GetGame() && GetGame().GetCallqueue())
		{
			GetGame().GetCallqueue().Remove(DeferredInit);
			GetGame().GetCallqueue().Remove(TryRegisterWithNetworkComponent);
			GetGame().GetCallqueue().Remove(RegisterActionListeners);
			GetGame().GetCallqueue().Remove(RegisterWithIRSystem);
			GetGame().GetCallqueue().Remove(ApplyAllCurrentState);
			GetGame().GetCallqueue().Remove(CheckWeaponState);
			GetGame().GetCallqueue().Remove(CheckReadiness);
		}
		
		UnregisterFromIRSystem();
		
		if (m_bActionListenerRegistered)
			UnregisterActionListeners();

		if (m_bFlashlightOn)
		{
			m_bFlashlightOn = false;
			ApplyFlashlightState(false);
		}

		if (m_bIROn)
		{
			m_bIROn = false;
			ApplyIRState(false);
		}

		if (m_mLights)
		{
			for (int i = 0; i < m_mLights.Count(); i++)
			{
                                IEntity entity = m_mLights.GetElement(i);
                                if (entity)
                                        SCR_EntityHelper.DeleteEntityAndChildren(entity);
                        }
                        m_mLights.Clear();
                }

                m_mLights = null;
        }
}
