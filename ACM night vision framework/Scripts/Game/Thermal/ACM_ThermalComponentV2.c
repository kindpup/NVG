// ACM Thermal Component V2
// Separate thermal gadget using ThermalImaging post-process and selectable palettes
//
// STRUCTURE: this component deliberately mirrors ACM_NightVisionComponent step for step -
// same deferred owner resolution, same ModeSwitch/ModeClear handling, same worn debounce,
// same RegisterInputs/ActivateContext pairing inside Update(). The only differences are
// the ones that are actually about thermal rather than night vision: the palette list,
// the slot 11 ThermalImaging material, and the action names.
//
// It previously carried a bespoke per-frame callqueue "input pump" built on the ASSUMPTION
// that sharing EGadgetType.NIGHT_VISION with ACM_NightVisionComponent on the same entity
// left this component undriven by the gadget manager. That assumption was never verified,
// and the evidence contradicts it: ModeClear demonstrably fires on this component, so the
// manager does track it. The pump was removed along with its polling, debounce and
// heartbeat scaffolding - all of which existed only to work around the unverified problem,
// and one of which (a StopInputPump call in ModeClear) was itself silently disabling the
// keybinds every time the device was equipped.

//! ECOTI-style modes of the fused thermal circle (F-Pano). The thermal key cycles
//! off -> OUTLINE -> FUSED -> THERMAL -> off.
enum ACM_EEcotiMode
{
	OUTLINE,	//!< bright edges around warm / contrasting things, night vision untouched elsewhere
	FUSED,		//!< thermal picture blended over the night vision
	THERMAL		//!< full thermal picture in the circle
}

[ComponentEditorProps(category: "ACM/Equipment", description: "Thermal imaging gadget component")]
class ACM_ThermalComponentV2Class : SCR_GadgetComponentClass
{
}

class ACM_ThermalComponentV2 : SCR_GadgetComponent
{
	[Attribute("", UIWidgets.Auto, desc: "Available thermal palettes", category: "ACM Thermal - Palettes")]
	protected ref array<ref ACM_ThermalPalette> m_aPalettes;
	
	[Attribute("0", UIWidgets.Slider, desc: "Default palette index", category: "ACM Thermal - Palettes", params: "0 31 1")]
	protected int m_iDefaultPalette;
	
	[Attribute("ACM_Thermal_Toggle", UIWidgets.EditBox, desc: "Toggle thermal action", category: "ACM Thermal - Input")]
	protected string m_sToggleAction;
	
	[Attribute("ACM_Thermal_Palette", UIWidgets.EditBox, desc: "Cycle thermal palette action", category: "ACM Thermal - Input")]
	protected string m_sPaletteAction;
	
	[Attribute("ACM_Thermal_Increase", UIWidgets.EditBox, desc: "Increase brightness action", category: "ACM Thermal - Input")]
	protected string m_sIncreaseAction;
	
	[Attribute("ACM_Thermal_Decrease", UIWidgets.EditBox, desc: "Decrease brightness action", category: "ACM Thermal - Input")]
	protected string m_sDecreaseAction;
	
	[Attribute("ACM_Thermal_Reset", UIWidgets.EditBox, desc: "Reset brightness action", category: "ACM Thermal - Input")]
	protected string m_sResetAction;
	
	[Attribute("1.5", UIWidgets.Slider, desc: "Base brightness", category: "ACM Thermal - Brightness", params: "0.05 20 0.05")]
	protected float m_fBaseBrightness;
	
	[Attribute("0", UIWidgets.CheckBox, desc: "Allow manual brightness adjustment", category: "ACM Thermal - Brightness")]
	protected bool m_bManualBrightnessEnabled;
	
	[Attribute("0.25", UIWidgets.Slider, desc: "Brightness step", category: "ACM Thermal - Brightness", params: "0.05 5 0.05")]
	protected float m_fBrightnessStep;
	
	[Attribute("0.05", UIWidgets.Slider, desc: "Lowest allowed brightness", category: "ACM Thermal - Brightness", params: "0.01 2 0.01")]
	protected float m_fMinBrightness;
	
	[Attribute("20", UIWidgets.Slider, desc: "Highest allowed brightness", category: "ACM Thermal - Brightness", params: "1 25 0.5")]
	protected float m_fMaxBrightness;
	
	[Attribute("", UIWidgets.Auto, desc: "Procedural animation signal identifier", category: "ACM Thermal - Animation")]
	protected string m_sAnimationSignal;

	// Tube view - thermal only inside the tube circles, normal view outside them. Same system as
	// ACM_NightVisionComponent (see ACM_NVGTubeView). FULLSCREEN keeps the old whole-screen effect.
	[Attribute("0", UIWidgets.ComboBox, desc: "Tube shape. FULLSCREEN = thermal covers the whole screen (old behaviour). SINGLE / DUAL / PANORAMIC = thermal only inside the tubes, normal view outside. CUSTOM = use Custom Tubes.", category: "ACM Thermal - Tubes", enums: ParamEnumArray.FromEnum(ACM_ENVGTubeLayout))]
	protected ACM_ENVGTubeLayout m_eTubeLayout;

	[Attribute("", UIWidgets.Object, desc: "Tube circles used when Tube Layout is CUSTOM. Units are screen heights from the screen centre.", category: "ACM Thermal - Tubes")]
	protected ref array<ref ACM_NVGTube> m_aCustomTubes;

	[Attribute("0.08", UIWidgets.Slider, desc: "How soft the tube edge is, as a fraction of the tube radius.", category: "ACM Thermal - Tubes", params: "0 0.5 0.01")]
	protected float m_fTubeEdgeSoftness;

	// Note: the thermal sky flicker is NOT caused by this or by the tube camera - it also happens
	// full-screen on the main camera. It is the engine's volumetric clouds inside ThermalImaging.
	[Attribute("1", UIWidgets.Slider, desc: "Render resolution of the thermal picture. Lower values are cheaper but blurrier.", category: "ACM Thermal - Tubes", params: "0.3 1 0.05")]
	protected float m_fTubeResolutionScale;

	[Attribute("0.12", UIWidgets.Slider, desc: "Lens distortion towards the tube edge. 0 = none.", category: "ACM Thermal - Tubes", params: "0 0.5 0.01")]
	protected float m_fTubeEdgeDistortion;

	[Attribute("1", UIWidgets.Slider, desc: "Darkness of the thin faded black outline around each tube. 0 = no outline.", category: "ACM Thermal - Tubes", params: "0 1 0.05")]
	protected float m_fTubeRimDarkness;

	[Attribute("1", UIWidgets.CheckBox, desc: "Clear sky: hide the clouds while thermal is on (local to this player only). The engine renders clouds unstably in thermal, which makes the whole sky flicker.", category: "ACM Thermal - Tubes")]
	protected bool m_bClearSky;

	// FUSED THERMAL (F-Pano): instead of turning the whole picture thermal, the thermal key adds a
	// thermal overlay to the night vision - a smaller circle inside the centre tube, blended over the
	// night vision, showing only warm things. Needs the night vision tubes to be on.
	[Attribute("0", UIWidgets.CheckBox, desc: "Fused thermal (F-Pano): the thermal key overlays thermal on the night vision in a smaller circle inside the centre tube, instead of switching the whole view to thermal.", category: "ACM Thermal - Fusion")]
	protected bool m_bFusionOverlay;

	[Attribute("0", UIWidgets.CheckBox, desc: "ECOTI outline only: the thermal key toggles just the outline overlay on and off (no Fused / Thermal palette modes). Needs Fused thermal on.", category: "ACM Thermal - Fusion")]
	protected bool m_bEcotiOutlineOnly;

	[Attribute("0.8", UIWidgets.Slider, desc: "Size of the fused thermal circle as a fraction of the centre tube.", category: "ACM Thermal - Fusion", params: "0.2 1 0.01")]
	protected float m_fFusionRadius;

	[Attribute("0.5", UIWidgets.Slider, desc: "Opacity of the fused thermal overlay over the night vision.", category: "ACM Thermal - Fusion", params: "0 1 0.05")]
	protected float m_fFusionOpacity;

	[Attribute("{0549C113CEA9243E}UI/Materials/Editor/Thermal Fusion.emat", UIWidgets.ResourceNamePicker, desc: "Thermal material for the fused overlay: should be black except for hot temperatures, so only warm things show.", category: "ACM Thermal - Fusion", params: "emat")]
	protected ResourceName m_FusionMaterial;

	[Attribute("0.5", UIWidgets.Slider, desc: "Opacity of the thermal picture over the night vision in FUSED mode.", category: "ACM Thermal - Fusion", params: "0.1 1 0.05")]
	protected float m_fFusedOpacity;

	protected bool m_bFusionApplied;
	protected bool m_bFusionFailLogged;

	//! Current ECOTI mode while the circle is on.
	protected ACM_EEcotiMode m_eEcotiMode = ACM_EEcotiMode.OUTLINE;
	//! Per-mode overlay strength (brightness keys), indexed by ACM_EEcotiMode.
	protected ref array<float> m_aEcotiStrength;
	protected static const float ECOTI_STRENGTH_STEP = 0.1;
	protected static const float ECOTI_STRENGTH_MIN = 0.1;

	protected bool m_bUsingTubeView;

	protected SignalsManagerComponent m_pSignalMgr;
	protected InventoryItemComponent m_pInventoryItem;
	protected SCR_CharacterControllerComponent m_pCharController;
	
	protected bool m_bIsWorn;
	protected bool m_bMenuOpen;
	protected bool m_bInputsRegistered;
	protected bool m_bSlot11Applied;
	protected bool m_bSlot16Applied;
	protected bool m_bHDRBrightnessApplied;
	protected int m_iAppliedCamId = -1;
	protected int m_iCurrentPalette;
	protected float m_fCurrentBrightness = -1;
	protected float m_fBrightnessCooldown;
	protected static const float BRIGHTNESS_ADJUST_RATE = 0.05;

	// Retry counter for deferred init - matches the NVG component.
	protected int m_iDeferredKitRetryCount;
	protected static const int MAX_DEFERRED_RETRIES = 20;

	// Worn-state stability. Brief inventory glitches report the device as unworn for a
	// frame or two; without this the thermal effect flickers off during ordinary
	// inventory operations. Mirrors the NVG component's debounce.
	protected int m_iNotWornFrameCount;
	protected static const int WORN_DEBOUNCE_FRAMES = 10;

	// Inventory/storage worn-state checks are throttled rather than run per frame.
	protected float m_fWornCheckTimer;
	protected static const float WORN_CHECK_INTERVAL = 0.25;

	// World time of the last DriveUpdate, used to collapse duplicate ticks within one frame.
	//
	// This component is reachable from two directions: its own Update (the gadget manager
	// does drive it - confirmed by a "ModeSwitch (self-driven)" trace) and the forwarded
	// HostUpdate from ACM_NightVisionComponent. Ticking twice per frame halves the
	// brightness cooldown and doubles the worn-debounce rate, so brightness ramps at twice
	// the configured speed.
	protected float m_fLastDriveTime = -1;

	// Set true to trace this component's lifecycle. Leave false in shipping builds.
	protected static const bool DEBUG_TRACE = false;

	// True once this component has received a ModeSwitch of its own, i.e. the gadget
	// manager is driving it directly.
	//
	// On the FPGPNVG it never is. This component and ACM_NightVisionComponent sit on the
	// same entity and both report EGadgetType.NIGHT_VISION; the manager routes mode changes
	// to only the first gadget of a type, so NVG receives ModeSwitch and this one receives
	// nothing but ModeClear. Without ModeSwitch there is no ActivateGadgetUpdate, so no
	// Update, so no ActivateContext - and an action in a context that was never activated
	// reads zero, which is indistinguishable from an unbound key.
	//
	// So the NVG component forwards its own lifecycle here (see ACM_NightVisionComponent),
	// and this flag is what stops us being driven twice should the manager ever start
	// delivering ModeSwitch directly - on a thermal-only device with no NVG component, for
	// instance, where this component would win the type slot itself.
	protected bool m_bSelfDriven;

	// Short per-instance tag. More than one of these exists at a time: Workbench spawns a
	// copy into its prefab preview 'ContainerWorld' whenever the resource browser touches
	// the prefab, alongside the real one in 'world'. Without a tag their trace lines
	// interleave and read as though a single component contradicts itself.
	protected static int s_iInstanceCounter;
	protected int m_iInstanceId;
	
	protected static const ResourceName DEFAULT_HDR = "{9DEECCABE8357209}Common/Postprocess/HDR.emat";

	// The ONLY material in this mod that is actually a ThermalImagingEffect, which is what
	// postprocess slot 11 requires.
	protected static const ResourceName DEFAULT_THERMAL_MATERIAL = "{A131B20E9E1745E0}UI/Materials/Editor/Thermal WH.emat";

	// Naming convention that lets new palettes be added without touching script: any
	// material under this path is trusted as a slot-11 material.
	//
	// This was previously the literal "Thermal WH", which only matched the one material
	// that existed at the time. Thermal Flir/Green/Orange would each have failed the check
	// and been silently replaced with White Hot - every palette in the radial menu would
	// have selected correctly and then looked identical.
	//
	// Matching the folder rather than the filename means new palettes work by being saved
	// in the right place, which is where they belong anyway.
	protected static const string THERMAL_IMAGING_MARKER = "UI/Materials/Editor/Thermal ";

	//------------------------------------------------------------------------------------------------
	// Slot 11 takes a ThermalImagingEffect material and NOTHING else. Handing it an
	// HDREffect or - worse - a MatPBRBasic surface material makes the engine cast it to a
	// type it isn't and read fields that do not exist, which crashes the client natively on
	// the next render. That is the crash the old code worked around by disabling slot 11
	// entirely; the real cause was the palettes pointing at Assets/SharedMaterials/*.emat,
	// which are MatPBRBasic.
	//
	// Since a material's class cannot be inspected from script, this is an allowlist rather
	// than a blocklist: anything not recognised is replaced with the known-good material and
	// logged, because a wrong guess here is a hard crash rather than a visual glitch.
	//------------------------------------------------------------------------------------------------
	protected bool IsThermalImagingMaterial(ResourceName material)
	{
		string matPath = material;
		if (matPath.IsEmpty())
			return false;

		if (matPath == DEFAULT_THERMAL_MATERIAL)
			return true;

		return matPath.Contains(THERMAL_IMAGING_MARKER);
	}

	protected int GetValidPaletteIndex(int requestedIndex)
	{
		if (!m_aPalettes || m_aPalettes.IsEmpty())
			return -1;

		if (requestedIndex < 0 || requestedIndex >= m_aPalettes.Count())
			return 0;

		return requestedIndex;
	}
	
	override EGadgetType GetType()
	{
		return EGadgetType.NIGHT_VISION;
	}
	
	//------------------------------------------------------------------------------------------------
	// Initialization
	//------------------------------------------------------------------------------------------------
	protected override void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);
		
		m_pInventoryItem = InventoryItemComponent.Cast(owner.FindComponent(InventoryItemComponent));
		m_pSignalMgr = SignalsManagerComponent.Cast(owner.FindComponent(SignalsManagerComponent));
		m_fCurrentBrightness = m_fBaseBrightness;
		m_iCurrentPalette = GetValidPaletteIndex(m_iDefaultPalette);

		s_iInstanceCounter++;
		m_iInstanceId = s_iInstanceCounter;

		DebugTrace("EOnInit palettes=" + GetPaletteCount());

		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().CallLater(DeferredInit, 500, false);
	}
	
	//------------------------------------------------------------------------------------------------
	// Deferred check for saved kit loading. Mirrors ACM_NightVisionComponent.DeferredKitLoadCheck.
	//------------------------------------------------------------------------------------------------
	protected void DeferredInit()
	{
		if (m_bInputsRegistered)
			return;
		
		if (!m_CharacterOwner)
		{
			IEntity localPlayer = SCR_PlayerController.GetLocalControlledEntity();
			if (localPlayer)
			{
				ChimeraCharacter character = ChimeraCharacter.Cast(localPlayer);
				if (character && IsOwnedByCharacter(character))
					m_CharacterOwner = character;
			}
			
			// Second attempt: walk up from the device itself.
			//
			// IsOwnedByCharacter only follows INVENTORY storage slots. This device hangs
			// off a headstrap as a child entity, so its GetParentSlot() is null on the very
			// first hop and that walk finds nothing. Alternating between the parent chain
			// and the storage chain covers both mounting styles.
			if (!m_CharacterOwner)
			{
				IEntity current = GetOwner();
				int depth = 0;
				while (current && depth < 15)
				{
					ChimeraCharacter walked = ChimeraCharacter.Cast(current);
					if (walked)
					{
						m_CharacterOwner = walked;
						break;
					}

					IEntity parent = current.GetParent();
					if (parent)
					{
						current = parent;
						depth++;
						continue;
					}

					InventoryItemComponent itemComp = InventoryItemComponent.Cast(current.FindComponent(InventoryItemComponent));
					if (itemComp)
					{
						InventoryStorageSlot slot = itemComp.GetParentSlot();
						if (slot)
						{
							BaseInventoryStorageComponent storage = slot.GetStorage();
							if (storage)
							{
								current = storage.GetOwner();
								depth++;
								continue;
							}
						}
					}

					break;
				}
			}
		}
		
		if (!m_CharacterOwner)
		{
			m_iDeferredKitRetryCount++;
			if (m_iDeferredKitRetryCount >= MAX_DEFERRED_RETRIES)
			{
				DebugTrace("gave up resolving owner (device is not carried by the local player)");
				return;
			}
			
			if (GetGame() && GetGame().GetCallqueue())
				GetGame().GetCallqueue().CallLater(DeferredInit, 500, false);
			return;
		}
		
		if (m_CharacterOwner != SCR_PlayerController.GetLocalControlledEntity())
			return;
		
		m_bIsWorn = CheckIsWorn();
		DebugTrace("DeferredInit resolved owner, worn=" + m_bIsWorn);
		
		if (m_bIsWorn && !m_bInputsRegistered)
		{
			// Update() only runs after this. It is what drives ActivateContext, and an
			// action in a context that was never activated reads zero forever.
			ActivateGadgetUpdate();
			RegisterInputs();
			
			m_pCharController = SCR_CharacterControllerComponent.Cast(m_CharacterOwner.GetCharacterController());
			if (m_pCharController)
				m_pCharController.m_OnPlayerDeath.Insert(OnDeath);
		}
	}
	
	protected bool IsOwnedByCharacter(ChimeraCharacter character)
	{
		if (!character || !m_pInventoryItem)
			return false;
		
		InventoryItemComponent itemComp = m_pInventoryItem;
		int depth = 0;
		while (itemComp && depth < 15)
		{
			InventoryStorageSlot slot = itemComp.GetParentSlot();
			if (!slot)
				break;
			
			BaseInventoryStorageComponent storage = slot.GetStorage();
			if (!storage)
				break;
			
			IEntity storageOwner = storage.GetOwner();
			if (!storageOwner)
				break;
			
			if (storageOwner == character)
				return true;
			
			itemComp = InventoryItemComponent.Cast(storageOwner.FindComponent(InventoryItemComponent));
			depth++;
		}
		return false;
	}
	
	//------------------------------------------------------------------------------------------------
	// Public accessors
	//------------------------------------------------------------------------------------------------
	float GetBrightness() { return m_fCurrentBrightness; }
	bool IsThermalActive() { return m_bActivated; }
	bool IsThermalWorn() { return m_bIsWorn; }
	int GetCurrentPaletteIndex() { return m_iCurrentPalette; }
	
	int GetPaletteCount()
	{
		if (!m_aPalettes)
			return 0;
		
		return m_aPalettes.Count();
	}
	
	array<string> GetPaletteNames()
	{
		array<string> names = {};
		if (!m_aPalettes)
			return names;
		
		foreach (ACM_ThermalPalette palette : m_aPalettes)
		{
			if (palette)
				names.Insert(palette.GetName());
		}
		return names;
	}
	
	string GetCurrentPaletteName()
	{
		if (!m_aPalettes || m_aPalettes.IsEmpty())
			return "Default";
		if (m_iCurrentPalette < 0 || m_iCurrentPalette >= m_aPalettes.Count())
			return "Default";
		ACM_ThermalPalette palette = m_aPalettes[m_iCurrentPalette];
		if (!palette)
			return "Default";
		return palette.GetName();
	}
	
	protected void DebugTrace(string message)
	{
		if (DEBUG_TRACE)
			Print("[ACM_ThermalComponentV2 #" + m_iInstanceId + "] " + message);
	}
	
	//------------------------------------------------------------------------------------------------
	// Public toggle methods for the radial menu
	//------------------------------------------------------------------------------------------------
	void ToggleThermal()
	{
		m_bIsWorn = CheckIsWorn();
		if (!m_bIsWorn)
			return;
		OnToggleInput();
	}
	
	void CyclePalette()
	{
		if (!m_aPalettes || m_aPalettes.IsEmpty())
			return;
		m_iCurrentPalette = (m_iCurrentPalette + 1) % m_aPalettes.Count();
		if (m_bActivated)
			ApplyVisualEffect(true, true);
	}
	
	void SetPalette(int paletteIndex)
	{
		if (!m_aPalettes || m_aPalettes.IsEmpty())
			return;
		if (paletteIndex < 0 || paletteIndex >= m_aPalettes.Count())
			paletteIndex = 0;
		m_iCurrentPalette = paletteIndex;
		if (m_bActivated)
			ApplyVisualEffect(true, true);
	}
	
	// Add a palette at runtime
	void AddPalette(ACM_ThermalPalette palette)
	{
		if (!palette)
			return;
		if (!m_aPalettes)
			m_aPalettes = new array<ref ACM_ThermalPalette>();
		m_aPalettes.Insert(palette);
	}
	
	// Remove a palette by index; keeps current index valid and re-applies if active
	void RemovePalette(int index)
	{
		if (!m_aPalettes || index < 0 || index >= m_aPalettes.Count())
			return;
		m_aPalettes.Remove(index);
		if (m_aPalettes.IsEmpty())
			m_iCurrentPalette = 0;
		else if (m_iCurrentPalette >= m_aPalettes.Count())
			m_iCurrentPalette = m_aPalettes.Count() - 1;
		if (m_bActivated)
			ApplyVisualEffect(true, true);
	}
	
	//------------------------------------------------------------------------------------------------
	// Manual brightness controls
	//------------------------------------------------------------------------------------------------
	protected void IncreaseBrightness()
	{
		// Traced before the guards, so the log distinguishes "the key never reached us"
		// from "it reached us and we declined to act because thermal is off".
		DebugTrace("IncreaseBrightness received (active=" + m_bActivated + " cooldown=" + m_fBrightnessCooldown + ")");

		if (!m_bActivated || m_fBrightnessCooldown > 0)
			return;
		m_fBrightnessCooldown = BRIGHTNESS_ADJUST_RATE;
		ModifyBrightness(true);
	}
	
	protected void DecreaseBrightness()
	{
		DebugTrace("DecreaseBrightness received (active=" + m_bActivated + " cooldown=" + m_fBrightnessCooldown + ")");

		if (!m_bActivated || m_fBrightnessCooldown > 0)
			return;
		m_fBrightnessCooldown = BRIGHTNESS_ADJUST_RATE;
		ModifyBrightness(false);
	}
	
	protected void ModifyBrightness(bool increase)
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world || !m_bActivated)
			return;

		// ECOTI: the keys set the strength of the current mode's overlay instead.
		if (m_bFusionOverlay)
		{
			AdjustEcotiStrength(increase);
			return;
		}

		float step = m_fBrightnessStep;
		if (!increase)
			step = -step;

		float newVal = Math.Clamp(m_fCurrentBrightness + step, m_fMinBrightness, m_fMaxBrightness);
		if (newVal == m_fCurrentBrightness)
		{
			DebugTrace("ModifyBrightness clamped at " + m_fCurrentBrightness + " (min=" + m_fMinBrightness + " max=" + m_fMaxBrightness + ")");
			return;
		}

		m_fCurrentBrightness = newVal;

		// Only update brightness on the camera we already applied to.
		if (m_bHDRBrightnessApplied && IsAppliedCameraLive(world))
		{
			world.SetCameraHDRBrightness(m_iAppliedCamId, m_fCurrentBrightness);
			DebugTrace("brightness -> " + m_fCurrentBrightness);
		}
		else
		{
			// Value moved but was not pushed to a camera. Worth saying out loud: it looks
			// identical in game to the key not working at all.
			DebugTrace("brightness changed to " + m_fCurrentBrightness + " but NOT applied (hdrApplied=" + m_bHDRBrightnessApplied + " appliedCam=" + m_iAppliedCamId + " currentCam=" + world.GetCurrentCameraId() + ")");
		}
	}
	
	protected void ResetBrightness()
	{
		DebugTrace("ResetBrightness received (active=" + m_bActivated + ")");

		if (!m_bActivated)
			return;

		// ECOTI: back to the authored strengths for every mode.
		if (m_bFusionOverlay)
		{
			m_aEcotiStrength = null;
			ApplyFusion(true);
			ShowEcotiHint(GetEcotiModeName() + "  reset");
			return;
		}

		// Use the active palette's default brightness first, fall back to component base
		if (m_aPalettes && m_iCurrentPalette >= 0 && m_iCurrentPalette < m_aPalettes.Count())
		{
			ACM_ThermalPalette palette = m_aPalettes[m_iCurrentPalette];
			if (palette)
				m_fCurrentBrightness = palette.GetDefaultBrightness();
			else
				m_fCurrentBrightness = m_fBaseBrightness;
		}
		else
		{
			m_fCurrentBrightness = m_fBaseBrightness;
		}
		
		BaseWorld world = GetGame().GetWorld();
		if (world && m_bHDRBrightnessApplied && IsAppliedCameraLive(world))
			world.SetCameraHDRBrightness(m_iAppliedCamId, m_fCurrentBrightness);
	}

	//------------------------------------------------------------------------------------------------
	// The tube camera is never the "current" camera, so the plain current-camera check would block
	// brightness changes whenever the tube view is in use.
	protected bool IsAppliedCameraLive(BaseWorld world)
	{
		if (m_iAppliedCamId < 0 || !world)
			return false;

		// Performance mode puts the chain on the main camera, which can change (vehicles), so check it.

		if (m_bUsingTubeView && !ACM_NVGTubeView.RendersOnMainCamera())
			return true;

		return m_iAppliedCamId == world.GetCurrentCameraId();
	}

	//------------------------------------------------------------------------------------------------
	protected void ReleaseTubeView()
	{
		ACM_NVGTubeView.Release(this);
		m_bUsingTubeView = false;
	}
	
	//------------------------------------------------------------------------------------------------
	// Toggle controls
	//------------------------------------------------------------------------------------------------
	protected void OnToggleInput()
	{
		DebugTrace("OnToggleInput received");
		ACM_MPDiag.Log("Thermal key on " + GetOwner().ToString() + ": menuOpen=" + m_bMenuOpen.ToString() + " owner=" + (m_CharacterOwner != null).ToString() + " ownerIsLocal=" + (m_CharacterOwner && m_CharacterOwner == SCR_PlayerController.GetLocalControlledEntity()).ToString() + " active=" + m_bActivated.ToString() + " fusion=" + m_bFusionOverlay.ToString());

		if (m_bMenuOpen)
			return;

		if (!m_CharacterOwner)
			return;

		if (m_CharacterOwner != SCR_PlayerController.GetLocalControlledEntity())
			return;

		m_bIsWorn = CheckIsWorn();
		if (!m_bIsWorn)
		{
			ACM_MPDiag.Log("Thermal key ignored: not worn");
			return;
		}
		ACM_MPDiag.Log("Thermal key accepted: tube view up=" + ACM_NVGTubeView.IsUp().ToString() + " character[" + ACM_MPDiag.Roles(m_CharacterOwner) + "] item[" + ACM_MPDiag.Roles(GetOwner()) + "]");

		// ECOTI: while the circle is on, the key steps OUTLINE -> FUSED -> THERMAL; one more press
		// after THERMAL switches it off (falls through below).
		// Outline-only devices (ECOTI without thermal palettes): the key just switches the outline on/off.
		if (m_bFusionOverlay && m_bActivated && m_eEcotiMode != ACM_EEcotiMode.THERMAL && !m_bEcotiOutlineOnly)
		{
			if (m_eEcotiMode == ACM_EEcotiMode.OUTLINE)
				m_eEcotiMode = ACM_EEcotiMode.FUSED;
			else
				m_eEcotiMode = ACM_EEcotiMode.THERMAL;

			ApplyFusion(true);
			ShowEcotiHint(GetEcotiModeName());
			return;
		}

		bool nextState = !m_bActivated;
		if (nextState && m_bFusionOverlay)
		{
			m_eEcotiMode = ACM_EEcotiMode.OUTLINE;

			// Fused thermal sits on top of the night vision, so the night vision has to be on.
			ACM_NightVisionComponent deviceNvg = ACM_NightVisionComponent.Cast(GetOwner().FindComponent(ACM_NightVisionComponent));
			if (deviceNvg && !deviceNvg.IsNVGActive())
				deviceNvg.ToggleNVG();
		}
		else if (nextState)
		{
			// Avoid visual conflicts: thermal owns the camera effect while active.
			ACM_NightVisionComponent nvgComp = ACM_NightVisionComponent.Cast(m_CharacterOwner.FindComponent(ACM_NightVisionComponent));
			if (nvgComp && nvgComp.IsNVGActive())
				nvgComp.ToggleNVG();
		}

		m_bActivated = nextState;

		string devicePrefab = "?";
		if (GetOwner() && GetOwner().GetPrefabData())
			devicePrefab = GetOwner().GetPrefabData().GetPrefabName();
		Print("[ACM Thermal] Thermal " + m_bActivated.ToString() + " on " + devicePrefab + " (fused overlay: " + m_bFusionOverlay.ToString() + ").", LogLevel.DEBUG);

		ApplyVisualEffect(m_bActivated);
		UpdateAnimation();
		ToggleActive(m_bActivated, SCR_EUseContext.CUSTOM);

		if (m_bFusionOverlay)
		{
			if (m_bActivated)
				ShowEcotiHint(GetEcotiModeName());
			else
				ShowEcotiHint("OFF");
		}
	}

	protected void OnPaletteInput()
	{
		if (!m_bActivated)
			return;
		CyclePalette();

		// ECOTI: the palette is the colour of the FUSED / THERMAL picture (OUTLINE keeps its own).
		if (m_bFusionOverlay)
			ShowEcotiHint("Palette: " + GetCurrentPaletteName());
	}
	
	override void OnToggleActive(bool state)
	{
		ACM_MPDiag.Log("Thermal OnToggleActive(" + state.ToString() + ") on " + GetOwner().ToString() + " ownerIsLocal=" + (m_CharacterOwner && m_CharacterOwner == SCR_PlayerController.GetLocalControlledEntity()).ToString());
		m_bActivated = state;
		ApplyVisualEffect(state);
		UpdateAnimation();
	}

	//------------------------------------------------------------------------------------------------
	// Visual effects
	//------------------------------------------------------------------------------------------------
	// Revert ONLY the postprocess slots/HDR brightness we previously wrote, and only on the
	// camera we wrote them to. Writing defaults to slots we never owned, or to a stale camId
	// after the active camera changed, corrupts another mod's or the engine's own
	// postprocess binding and crashes natively on the next render.
	protected void RevertAppliedEffects()
	{
		if (m_iAppliedCamId < 0)
		{
			m_bSlot11Applied = false;
			m_bSlot16Applied = false;
			m_bHDRBrightnessApplied = false;
			return;
		}

		BaseWorld world = GetGame().GetWorld();
		if (world)
		{
			if (m_bSlot11Applied)
				world.SetCameraPostProcessEffect(m_iAppliedCamId, 11, PostProcessEffectType.ThermalImaging, "");
			if (m_bSlot16Applied)
				world.SetCameraPostProcessEffect(m_iAppliedCamId, 16, PostProcessEffectType.HDR, DEFAULT_HDR);
			if (m_bHDRBrightnessApplied)
				world.SetCameraHDRBrightness(m_iAppliedCamId, -1);
		}

		m_bSlot11Applied = false;
		m_bSlot16Applied = false;
		m_bHDRBrightnessApplied = false;
		m_iAppliedCamId = -1;
	}
	
	void ApplyVisualEffect(bool enable, bool force = false)
	{
		if (!m_CharacterOwner)
			return;
		
		bool isLocalPlayer = (m_CharacterOwner == SCR_PlayerController.GetLocalControlledEntity());
		if (!force && !isLocalPlayer)
			return;
		
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return;
		
		int camId = world.GetCurrentCameraId();
		if (camId < 0)
			return;

		// Fused thermal (F-Pano) never touches the main or night vision camera: it only adds the
		// overlay to the night vision tubes.
		if (m_bFusionOverlay)
		{
			ApplyFusion(enable && m_bIsWorn && isLocalPlayer);
			return;
		}

		if (enable && m_bIsWorn)
		{
			// TUBE VIEW: the thermal chain goes on the tube camera and the main camera stays clean,
			// so only the inside of the tubes is thermal. Falls back to the main camera (the old
			// full-screen effect) if the view cannot be built.
			bool useTubes = false;
			if (isLocalPlayer && m_eTubeLayout != ACM_ENVGTubeLayout.FULLSCREEN)
			{
				if (ACM_NVGTubeView.Acquire(this, m_eTubeLayout, m_aCustomTubes, m_fTubeEdgeSoftness, m_fTubeResolutionScale, m_fTubeEdgeDistortion, m_fTubeRimDarkness))
				{
					camId = ACM_NVGTubeView.GetEffectCamera();
					useTubes = true;
				}
			}

			if (!useTubes)
				ReleaseTubeView();

			// Active camera changed since last apply -> revert the old camera first.
			if (m_iAppliedCamId >= 0 && m_iAppliedCamId != camId)
				RevertAppliedEffects();

			m_bUsingTubeView = useTubes;

			if (isLocalPlayer)
				SetDeviceVisible(false);

			m_iAppliedCamId = camId;
			m_iCurrentPalette = GetValidPaletteIndex(m_iCurrentPalette);
			
			ACM_ThermalPalette palette = null;
			if (m_aPalettes && m_iCurrentPalette >= 0 && m_iCurrentPalette < m_aPalettes.Count())
				palette = m_aPalettes[m_iCurrentPalette];
			
			ResourceName material = "";
			if (palette)
			{
				material = palette.GetMaterial();
				if (m_fCurrentBrightness < 0 || m_fCurrentBrightness < m_fMinBrightness)
					m_fCurrentBrightness = palette.GetDefaultBrightness();
			}
			
			if (material.IsEmpty())
			{
				DebugTrace("no palette material for index=" + m_iCurrentPalette + ", aborting enable");
				return;
			}

			if (!IsThermalImagingMaterial(material))
			{
				Print(string.Format("[ACM_ThermalComponentV2] Palette '%1' material is not a recognised ThermalImagingEffect and would crash postprocess slot 11: %2. Falling back to %3.",
					GetCurrentPaletteName(), material, DEFAULT_THERMAL_MATERIAL), LogLevel.WARNING);

				material = DEFAULT_THERMAL_MATERIAL;
			}
			
			// Old stable behavior: suppress weather-related post effects while thermal is active.
			world.SetCameraPostProcessEffect(camId, 4, PostProcessEffectType.Rain, "");
			world.SetCameraPostProcessEffect(camId, 2, PostProcessEffectType.UnderWater, "");
			
			world.SetCameraPostProcessEffect(camId, 11, PostProcessEffectType.ThermalImaging, material);
			m_bSlot11Applied = true;

			world.SetCameraPostProcessEffect(camId, 16, PostProcessEffectType.HDR, DEFAULT_HDR);
			m_bSlot16Applied = true;

			if (m_fCurrentBrightness < 0 || m_fCurrentBrightness < m_fMinBrightness)
			{
				if (palette)
					m_fCurrentBrightness = palette.GetDefaultBrightness();
				else
					m_fCurrentBrightness = m_fBaseBrightness;
			}

			world.SetCameraHDRBrightness(camId, m_fCurrentBrightness);
			m_bHDRBrightnessApplied = true;

			if (isLocalPlayer)
				ACM_ThermalClouds.SetClearSky(this, m_bClearSky);
		}
		else
		{
			if (isLocalPlayer)
				SetDeviceVisible(true);

			ACM_ThermalClouds.SetClearSky(this, false);
			RevertAppliedEffects();
			ReleaseTubeView();
			world.SetCameraPostProcessEffect(camId, 9, PostProcessEffectType.None, "");
			world.SetCameraPostProcessEffect(camId, 8, PostProcessEffectType.None, "");
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Fused thermal on/off. The overlay lives in the night vision's tube view; if that is not up
	//! (night vision off) it is applied later from DriveUpdate as soon as the view appears.
	protected void ApplyFusion(bool enable)
	{
		if (enable)
		{
			if (ACM_NVGTubeView.IsUp())
			{
				ResourceName material;
				float opacity;
				bool additive;
				GetEcotiSettings(material, opacity, additive);

				m_bFusionApplied = ACM_NVGTubeView.SetFusionOverlay(true, material, m_fFusionRadius, opacity, additive);
				if (!m_bFusionApplied && !m_bFusionFailLogged)
				{
					m_bFusionFailLogged = true;
					Print("[ACM Thermal] Fused overlay could not start (material: " + material + ").", LogLevel.WARNING);
				}
			}
			else
			{
				m_bFusionApplied = false;
				Print("[ACM Thermal] Fused overlay waiting for the night vision tubes.", LogLevel.DEBUG);
			}

			ACM_ThermalClouds.SetClearSky(this, m_bClearSky);
			return;
		}

		if (m_bFusionApplied)
			ACM_NVGTubeView.SetFusionOverlay(false, m_FusionMaterial);

		m_bFusionApplied = false;
		ACM_ThermalClouds.SetClearSky(this, false);
	}

	//------------------------------------------------------------------------------------------------
	// ECOTI MODES
	//------------------------------------------------------------------------------------------------
	protected void EnsureEcotiStrength()
	{
		if (m_aEcotiStrength && m_aEcotiStrength.Count() == 3)
			return;

		m_aEcotiStrength = {m_fFusionOpacity, m_fFusedOpacity, 1.0};
	}

	//------------------------------------------------------------------------------------------------
	//! Material, opacity and blend for the current mode. OUTLINE uses the outline material and is
	//! added on top; FUSED and THERMAL use the selected palette and cover the night vision.
	protected void GetEcotiSettings(out ResourceName material, out float opacity, out bool additive)
	{
		EnsureEcotiStrength();
		opacity = m_aEcotiStrength[m_eEcotiMode];

		if (m_eEcotiMode == ACM_EEcotiMode.OUTLINE)
		{
			material = m_FusionMaterial;
			additive = true;
			return;
		}

		additive = false;
		material = "";

		m_iCurrentPalette = GetValidPaletteIndex(m_iCurrentPalette);
		if (m_aPalettes && m_iCurrentPalette >= 0 && m_iCurrentPalette < m_aPalettes.Count() && m_aPalettes[m_iCurrentPalette])
			material = m_aPalettes[m_iCurrentPalette].GetMaterial();

		if (material.IsEmpty() || !IsThermalImagingMaterial(material))
			material = DEFAULT_THERMAL_MATERIAL;
	}

	//------------------------------------------------------------------------------------------------
	protected string GetEcotiModeName()
	{
		switch (m_eEcotiMode)
		{
			case ACM_EEcotiMode.OUTLINE: return "OUTLINE";
			case ACM_EEcotiMode.FUSED: return "FUSED";
			case ACM_EEcotiMode.THERMAL: return "THERMAL";
		}

		return "?";
	}

	//------------------------------------------------------------------------------------------------
	protected void ShowEcotiHint(string text)
	{
		SCR_HintManagerComponent.ShowCustomHint(text, "ECOTI", 1.5);
	}

	//------------------------------------------------------------------------------------------------
	//! Brightness keys in ECOTI: the strength of the current mode's overlay.
	protected void AdjustEcotiStrength(bool increase)
	{
		EnsureEcotiStrength();

		float step = ECOTI_STRENGTH_STEP;
		if (!increase)
			step = -step;

		float value = Math.Clamp(m_aEcotiStrength[m_eEcotiMode] + step, ECOTI_STRENGTH_MIN, 1.0);
		m_aEcotiStrength[m_eEcotiMode] = value;

		ApplyFusion(true);
		int percent = Math.Round(value * 100);
		ShowEcotiHint(GetEcotiModeName() + "  " + percent.ToString() + "%");
	}

	//------------------------------------------------------------------------------------------------
	// The tube view outlives nothing: if the device is deleted while thermal is on, drop the
	// effect from the tube camera and remove the view.
	override void OnDelete(IEntity owner)
	{
		if (m_bFusionOverlay)
			ApplyFusion(false);
		ACM_ThermalClouds.SetClearSky(this, false);

		if (m_bUsingTubeView)
			RevertAppliedEffects();
		ReleaseTubeView();

		super.OnDelete(owner);
	}
	
	protected void SetDeviceVisible(bool visible)
	{
	}
	
	protected void UpdateAnimation()
	{
		if (!m_pSignalMgr || m_sAnimationSignal.IsEmpty())
			return;
		// Thermal shares the rotator with night vision on the same device, and the night vision
		// component replicates the activation state that drives it. This one therefore also runs on
		// every proxy rather than only the wearer's machine - guarding it here while the NVG path is
		// unguarded would make the two disagree about where the visor is, and whichever wrote last
		// would win.
		int sigId = m_pSignalMgr.FindSignal(m_sAnimationSignal);
		if (sigId < 0)
			return;

		// Same inactive-entity problem the NVG path has: a worn item sits with ACTIVE cleared and
		// an inactive entity never ticks its ProcAnimComponent, so the write below would land in a
		// bone nothing evaluates. Assert it here too - cheap, idempotent, transitions only.
		IEntity animOwner = GetOwner();
		if (animOwner && (animOwner.GetFlags() & EntityFlags.ACTIVE) == 0)
			animOwner.SetFlags(EntityFlags.ACTIVE, false);

		// Polarity matches ACM_NightVisionComponent.UpdateAnimation: since the bind-pose re-rig,
		// 1 is DEPLOYED (+145) and 0 is the stowed rest pose - so this writes the activation state
		// directly rather than its inverse.
		m_pSignalMgr.SetSignalValue(sigId, m_bActivated);
	}
	
	//------------------------------------------------------------------------------------------------
	// Update loop. Mirrors ACM_NightVisionComponent.Update.
	//------------------------------------------------------------------------------------------------
	override void Update(float timeSlice)
	{
		super.Update(timeSlice);
		DriveUpdate(timeSlice);
	}

	//------------------------------------------------------------------------------------------------
	// Host driving hooks.
	//
	// Called by ACM_NightVisionComponent on the same entity, because the gadget manager
	// only drives one component per EGadgetType and NVG is the one it picks. These are the
	// same code paths the manager would call; nothing here is a workaround timer.
	//------------------------------------------------------------------------------------------------
	bool IsSelfDriven() { return m_bSelfDriven; }

	// No self-driven latch here on purpose.
	//
	// The manager's delivery is not consistent: on pickup this component received a
	// forwarded ModeSwitch (mode=1, hasCharOwner=1) that it never got directly, then
	// moments later received both a forwarded and a direct one for mode=0. Latching
	// "self-driven" after the first direct call would have thrown away the forwarded
	// events that actually arrive first.
	//
	// Being driven from both sides is safe because the work is idempotent: RegisterInputs
	// refuses to double-register (which is what made one keypress toggle twice and cancel
	// itself out), and DriveUpdate only ever reads state and re-activates the input
	// context, which must happen every frame regardless.
	void HostUpdate(float timeSlice)
	{
		DriveUpdate(timeSlice);
	}

	void HostModeSwitch(EGadgetMode mode, IEntity charOwner)
	{
		DriveModeSwitch(mode, charOwner);
	}

	void HostModeClear(EGadgetMode mode)
	{
		DriveModeClear(mode);
	}

	//------------------------------------------------------------------------------------------------
	protected void DriveUpdate(float timeSlice)
	{
		// Collapse duplicate ticks. World time is constant within a frame, so a second call
		// carrying the same stamp is the other driver, not the next frame.
		BaseWorld driveWorld = GetGame().GetWorld();
		if (driveWorld)
		{
			float nowWorld = driveWorld.GetWorldTime();
			if (nowWorld == m_fLastDriveTime)
				return;
			
			m_fLastDriveTime = nowWorld;
		}
		
		if (!m_CharacterOwner || m_CharacterOwner != SCR_PlayerController.GetLocalControlledEntity())
			return;
		
		if (m_fBrightnessCooldown > 0)
			m_fBrightnessCooldown -= timeSlice;
		
		bool wasWorn = m_bIsWorn;
		m_fWornCheckTimer += timeSlice;
		if (m_fWornCheckTimer >= WORN_CHECK_INTERVAL)
		{
			m_fWornCheckTimer = 0;
			m_bIsWorn = CheckIsWorn();
		}
		
		if (m_bIsWorn != wasWorn)
		{
			if (m_bIsWorn)
			{
				m_iNotWornFrameCount = 0;
				if (!m_bInputsRegistered)
				{
					RegisterInputs();
					
					if (!m_pCharController)
						m_pCharController = SCR_CharacterControllerComponent.Cast(m_CharacterOwner.GetCharacterController());
					if (m_pCharController)
						m_pCharController.m_OnPlayerDeath.Insert(OnDeath);
				}
			}
			else
			{
				m_iNotWornFrameCount = 1;
			}
		}
		
		if (!m_bIsWorn)
		{
			if (m_iNotWornFrameCount > 0 && m_iNotWornFrameCount < WORN_DEBOUNCE_FRAMES)
			{
				m_iNotWornFrameCount++;
				m_bIsWorn = true; // Keep on during debounce
			}
			else if (m_iNotWornFrameCount >= WORN_DEBOUNCE_FRAMES)
			{
				UnregisterInputs();
				m_bInputsRegistered = false;
				m_iNotWornFrameCount = 0;
				
				if (m_bActivated)
				{
					m_bActivated = false;
					ApplyVisualEffect(false, true);
				}
			}
			return;
		}
		
		// ACM_ThermalContext, not ACM_NVContext.
		//
		// The thermal actions are declared in ACM_ThermalContext in chimeraInputCommon.conf.
		// This has to be re-activated EVERY frame: a context is only considered active on
		// the frame ActivateContext is called, and an action in an inactive context reads
		// zero - which looks exactly like an unbound key.
		GetGame().GetInputManager().ActivateContext("ACM_ThermalContext");

		// Fused thermal follows the night vision: when the tubes (re)appear while fused thermal is on,
		// put the overlay back; when they go, it goes with them.
		if (m_bFusionOverlay && m_bActivated && !m_bMenuOpen)
		{
			if (ACM_NVGTubeView.IsUp() && !ACM_NVGTubeView.IsFusionOn())
				ApplyFusion(true);
			else if (!ACM_NVGTubeView.IsUp())
				m_bFusionApplied = false;
		}
		
		bool menuNowOpen = GetGame().GetMenuManager().IsAnyMenuOpen();
		if (menuNowOpen != m_bMenuOpen)
		{
			m_bMenuOpen = menuNowOpen;
			ApplyVisualEffect(!m_bMenuOpen && m_bActivated, true);
		}
	}
	
	//------------------------------------------------------------------------------------------------
	// Worn detection
	//------------------------------------------------------------------------------------------------
	//------------------------------------------------------------------------------------------------
	// WHICH CHARACTER IS CARRYING THIS DEVICE - ANY character, not just the local player.
	//
	// This is the fix for "other players cannot see NVGs mounted on helmets".
	//
	// The old lazy resolution inside CheckIsWorn only ever tested GetLocalControlledEntity(). On the
	// machine of the person wearing the device that is correct and everything worked. On every OTHER
	// machine it failed to resolve an owner, CheckIsWorn returned false, and UpdateVisibility read
	// that as "not worn" and called HideOwner() - so the device was explicitly hidden on every client
	// except the wearer's. It looked exactly like a replication failure. Nothing was failing to
	// replicate; the entity arrived and was then hidden by this mod.
	//
	// Helmets specifically, because the decision also consults GetParentSlot(). A device in its own
	// loadout area has no parent slot and escaped through the !isStoredInInventory branch; one
	// mounted inside a helmet's attachment storage has a parent slot and fell through to HideOwner.
	//
	// The walk alternates parent chain and storage chain, because the two mounting styles need
	// different hops: a headstrap device is a scene CHILD, a helmet-mounted device is in inventory
	// STORAGE, and neither walk alone reaches both.
	//------------------------------------------------------------------------------------------------
	protected ChimeraCharacter ResolveCharacterOwner()
	{
		if (m_CharacterOwner)
			return ChimeraCharacter.Cast(m_CharacterOwner);

		// Local player first: cheapest, and verified with IsOwnedByCharacter rather than assumed.
		// Assigning the local player WITHOUT that test - which is what the thermal component used to
		// do - makes every remote player's device believe the viewer is wearing it.
		IEntity localPlayer = SCR_PlayerController.GetLocalControlledEntity();
		if (localPlayer)
		{
			ChimeraCharacter localCharacter = ChimeraCharacter.Cast(localPlayer);
			if (localCharacter && IsOwnedByCharacter(localCharacter))
			{
				m_CharacterOwner = localCharacter;
				return localCharacter;
			}
		}

		IEntity current = GetOwner();
		int depth = 0;

		while (current && depth < 15)
		{
			ChimeraCharacter walked = ChimeraCharacter.Cast(current);
			if (walked)
			{
				m_CharacterOwner = walked;
				return walked;
			}

			IEntity parent = current.GetParent();
			if (parent)
			{
				current = parent;
				depth++;
				continue;
			}

			InventoryItemComponent itemComp = InventoryItemComponent.Cast(current.FindComponent(InventoryItemComponent));
			if (itemComp)
			{
				InventoryStorageSlot slot = itemComp.GetParentSlot();
				if (slot)
				{
					BaseInventoryStorageComponent storage = slot.GetStorage();
					if (storage)
					{
						current = storage.GetOwner();
						depth++;
						continue;
					}
				}
			}

			break;
		}

		return null;
	}

	protected bool CheckIsWorn()
	{
		if (!m_CharacterOwner)
			ResolveCharacterOwner();

		if (!m_CharacterOwner || !m_pInventoryItem)
			return false;
		EquipedLoadoutStorageComponent loadout = EquipedLoadoutStorageComponent.Cast(
			m_CharacterOwner.FindComponent(EquipedLoadoutStorageComponent));
		if (!loadout)
			return false;
		
		IEntity owner = GetOwner();
		
		// Quick checks first
		IEntity slottedNVG = loadout.GetClothFromArea(ACM_NV_ItemSlot);
		if (slottedNVG == owner)
			return true;
		
		IEntity equippedHeadstrap = loadout.GetClothFromArea(ACM_Headstrap);
		if (equippedHeadstrap == owner)
			return true;
		
		// THE TWO REAL HIERARCHIES, written down because getting this wrong breaks the toggle
		// rather than anything visible, which makes it expensive to diagnose:
		//
		//   helmet     character -> helmet -> NVG mount (pocket storage component) -> device
		//   headstrap  character -> headstrap -> device
		//
		// The helmet chain is INVENTORY STORAGE at every hop, so IsInStorageChain walks it exactly:
		// device -> mount, mount -> helmet, done. The headstrap chain can present as a scene child,
		// so it needs the parent walk as well.
		//
		// These specific checks run FIRST and are the ones that matter. A generic walk replaced them
		// once and broke helmets outright: it tried GetParent() before the storage hop, and a
		// slotted item's scene parent skips straight past the mount and the helmet, so the helmet
		// was never reached and CheckIsWorn returned false - which reads as "the NVG will not switch
		// on" rather than as a search failure.
		if (equippedHeadstrap && IsChildOfEntity(owner, equippedHeadstrap, 5))
			return true;

		if (equippedHeadstrap && IsInStorageChain(m_pInventoryItem, equippedHeadstrap, 10))
			return true;

		IEntity equippedHeadgear = loadout.GetClothFromArea(LoadoutHeadCoverArea);

		if (equippedHeadgear && IsInStorageChain(m_pInventoryItem, equippedHeadgear, 10))
			return true;

		// Symmetry with the headstrap, and purely additive: a helmet-mounted device that happens to
		// present as a scene child is now found too. Nothing that worked before depends on it.
		if (equippedHeadgear && IsChildOfEntity(owner, equippedHeadgear, 5))
			return true;

		// Last resort only, for a mount whose chain mixes hop types in an order the two walks above
		// cannot follow. Everything that resolved before this method existed has already returned by
		// now, so this can add answers and cannot take any away.
		if (IsMountedOn(equippedHeadstrap))
			return true;

		if (IsMountedOn(equippedHeadgear))
			return true;
		
		return false;
	}

	//------------------------------------------------------------------------------------------------
	// Is this device mounted on `target`, by ANY route?
	//
	// Written because the two existing walks each only understand half of how gear attaches, and a
	// custom mount framework uses both halves - often alternating, with an adapter entity in the
	// middle that carries no InventoryItemComponent at all.
	//
	// IsInStorageChain follows inventory slots and gives up the moment GetParentSlot() is null, so a
	// device parented as a scene child of a mount loses it on the first hop. IsChildOfEntity follows
	// scene parents and cannot cross an inventory slot at all. Neither reaches a device that is a
	// child of a mount that is itself slotted into a helmet - which is exactly the shape a modular
	// mount produces.
	//
	// So this tries BOTH hops at every step and only stops when neither is available. It makes no
	// assumption about how many links there are or what kind each one is, which is the point: the
	// mod does not own the mount prefabs and should not be encoding their structure.
	//------------------------------------------------------------------------------------------------
	protected bool IsMountedOn(IEntity target, int maxDepth = 12)
	{
		if (!target)
			return false;

		IEntity current = GetOwner();
		int depth = 0;

		while (current && depth < maxDepth)
		{
			if (current == target)
				return true;

			// STORAGE HOP FIRST. This ordering is the whole correctness of the method.
			//
			// A slotted item also has a scene parent, and that parent is NOT its container - it is
			// wherever the engine attached it, typically a bone on the character. Taking the parent
			// hop first therefore leaps over every intermediate container in one step: from the
			// device straight past the mount and the helmet. The device is then never found on the
			// helmet, and the storage chain that actually describes the mount is never walked.
			InventoryItemComponent itemComp = InventoryItemComponent.Cast(current.FindComponent(InventoryItemComponent));
			if (itemComp)
			{
				InventoryStorageSlot slot = itemComp.GetParentSlot();
				if (slot)
				{
					BaseInventoryStorageComponent storage = slot.GetStorage();
					if (storage && storage.GetOwner())
					{
						current = storage.GetOwner();
						depth++;
						continue;
					}
				}
			}

			// Only once there is no container left to climb: the scene parent, for gear that hangs
			// off something rather than sitting in it.
			IEntity parent = current.GetParent();
			if (!parent)
				return false;

			current = parent;
			depth++;
		}

		return false;
	}
	
	protected bool IsChildOfEntity(IEntity entity, IEntity target, int maxDepth)
	{
		if (!entity || !target)
			return false;
		
		IEntity parent = entity.GetParent();
		int depth = 0;
		while (parent && depth < maxDepth)
		{
			if (parent == target)
				return true;
			parent = parent.GetParent();
			depth++;
		}
		return false;
	}
	
	protected bool IsInStorageChain(InventoryItemComponent itemComp, IEntity target, int maxDepth)
	{
		if (!itemComp || !target)
			return false;
		
		int depth = 0;
		while (itemComp && depth < maxDepth)
		{
			InventoryStorageSlot slot = itemComp.GetParentSlot();
			if (!slot)
				break;
			
			BaseInventoryStorageComponent storage = slot.GetStorage();
			if (!storage)
				break;
			
			IEntity storageOwner = storage.GetOwner();
			if (!storageOwner)
				break;
			
			if (storageOwner == target)
				return true;
			
			if (storageOwner == m_CharacterOwner)
				break;
			
			itemComp = InventoryItemComponent.Cast(storageOwner.FindComponent(InventoryItemComponent));
			depth++;
		}
		return false;
	}
	
	//------------------------------------------------------------------------------------------------
	// Input registration
	//------------------------------------------------------------------------------------------------
	protected void RegisterInputs()
	{
		// Idempotent, and that is load bearing rather than defensive.
		//
		// AddActionListener does not deduplicate. Registering the same listener twice means
		// one keypress invokes OnToggleInput twice - thermal switches on and immediately
		// back off within the same frame, which looks exactly like a dead keybind while the
		// radial menu (a single direct call) keeps working. ModeSwitch can legitimately
		// arrive more than once, so the guard belongs here rather than at each call site.
		if (m_bInputsRegistered)
		{
			DebugTrace("RegisterInputs skipped - already registered");
			return;
		}

		InputManager im = GetGame().GetInputManager();
		if (!im)
			return;

		m_bInputsRegistered = true;
		DebugTrace("RegisterInputs toggle='" + m_sToggleAction + "'");
		ACM_MPDiag.Log("Thermal keys registered on " + GetOwner().ToString());

		im.AddActionListener(m_sToggleAction, EActionTrigger.DOWN, OnToggleInput);
		if (m_aPalettes && m_aPalettes.Count() > 1)
			im.AddActionListener(m_sPaletteAction, EActionTrigger.DOWN, OnPaletteInput);
		if (m_bManualBrightnessEnabled)
		{
			im.AddActionListener(m_sIncreaseAction, EActionTrigger.PRESSED, IncreaseBrightness);
			im.AddActionListener(m_sDecreaseAction, EActionTrigger.PRESSED, DecreaseBrightness);
			im.AddActionListener(m_sResetAction, EActionTrigger.DOWN, ResetBrightness);
		}
	}
	
	protected void UnregisterInputs()
	{
		InputManager im = GetGame().GetInputManager();
		if (!im)
			return;

		m_bInputsRegistered = false;
		im.RemoveActionListener(m_sToggleAction, EActionTrigger.DOWN, OnToggleInput);
		if (m_aPalettes && m_aPalettes.Count() > 1)
			im.RemoveActionListener(m_sPaletteAction, EActionTrigger.DOWN, OnPaletteInput);
		if (m_bManualBrightnessEnabled)
		{
			im.RemoveActionListener(m_sIncreaseAction, EActionTrigger.PRESSED, IncreaseBrightness);
			im.RemoveActionListener(m_sDecreaseAction, EActionTrigger.PRESSED, DecreaseBrightness);
			im.RemoveActionListener(m_sResetAction, EActionTrigger.DOWN, ResetBrightness);
		}
	}
	
	//------------------------------------------------------------------------------------------------
	// Events
	//------------------------------------------------------------------------------------------------
	protected void OnDeath()
	{
		m_bMenuOpen = false;
		m_bIsWorn = false;
		m_iNotWornFrameCount = 0;
		
		ModeClear(EGadgetMode.LAST);
		
		if (m_bActivated)
		{
			m_bActivated = false;
			ApplyVisualEffect(false, true);
		}
	}
	
	override protected void ModeSwitch(EGadgetMode mode, IEntity charOwner)
	{
		// Reaching here at all means the manager is driving us directly, so stop accepting
		// forwarded calls from the NVG component.
		m_bSelfDriven = true;
		DebugTrace("ModeSwitch (self-driven) mode=" + mode);

		super.ModeSwitch(mode, charOwner);
		DriveModeSwitch(mode, charOwner);
	}

	//------------------------------------------------------------------------------------------------
	protected void DriveModeSwitch(EGadgetMode mode, IEntity charOwner)
	{
		DebugTrace("ModeSwitch mode=" + mode + " hasCharOwner=" + (charOwner != null));

		if (mode == EGadgetMode.ON_GROUND || mode == EGadgetMode.LAST)
		{
			m_bIsWorn = false;
			if (m_bActivated)
			{
				m_bActivated = false;
				ApplyVisualEffect(false, true);
			}
			m_bInputsRegistered = false;
			m_iNotWornFrameCount = 0;
			UnregisterInputs();
			return;
		}
		
		if (!charOwner)
			return;
		
		m_CharacterOwner = ChimeraCharacter.Cast(charOwner);
		if (!m_CharacterOwner)
			return;
		
		// Update() only runs after this, and Update() is what activates the input context.
		ActivateGadgetUpdate();
		
		bool wasWorn = m_bIsWorn;
		m_bIsWorn = CheckIsWorn();
		
		bool isLocalPlayer = (m_CharacterOwner == SCR_PlayerController.GetLocalControlledEntity());
		
		UpdateAnimation();
		
		if (m_pInventoryItem)
			m_pInventoryItem.ActivateOwner(true);
		
		m_pCharController = SCR_CharacterControllerComponent.Cast(m_CharacterOwner.GetCharacterController());
		
		if (isLocalPlayer && m_bIsWorn)
		{
			bool wasRegistered = m_bInputsRegistered;
			RegisterInputs();
			
			// Only hook death once, for the same reason inputs are only registered once.
			if (!wasRegistered && m_pCharController)
				m_pCharController.m_OnPlayerDeath.Insert(OnDeath);
		}
		else if (isLocalPlayer && !m_bIsWorn && wasWorn)
		{
			UnregisterInputs();
			m_bInputsRegistered = false;
			
			if (m_bActivated)
			{
				m_bActivated = false;
				ApplyVisualEffect(false, true);
			}
		}
	}
	
	override void UpdateVisibility(EGadgetMode mode)
	{
		if (m_pInventoryItem)
			m_pInventoryItem.ShowOwner();
	}
	
	override void ModeClear(EGadgetMode mode)
	{
		DriveModeClear(mode);
	}

	//------------------------------------------------------------------------------------------------
	protected void DriveModeClear(EGadgetMode mode)
	{
		DebugTrace("ModeClear mode=" + mode);

		if (m_bFusionOverlay)
			ApplyFusion(false);
		ACM_ThermalClouds.SetClearSky(this, false);
		RevertAppliedEffects();
		UnregisterInputs();
		m_bInputsRegistered = false;
		DeactivateGadgetUpdate();
		
		if (m_CharacterOwner && m_pCharController)
			m_pCharController.m_OnPlayerDeath.Remove(OnDeath);
		
		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().Remove(DeferredInit);
	}
}
