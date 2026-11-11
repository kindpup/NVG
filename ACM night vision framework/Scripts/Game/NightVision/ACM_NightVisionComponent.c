// ACM Night Vision Gadget - Core vision enhancement component
// Handles NVG effects, gain control, and thermal mode

//------------------------------------------------------------------------------------------------
// Phosphor color option class for dynamic color configuration
//------------------------------------------------------------------------------------------------
[BaseContainerProps(), BaseContainerCustomTitleField("m_sName")]
class ACM_PhosphorColorOption
{
	[Attribute("", UIWidgets.EditBox, desc: "Display name for this color")]
	protected string m_sName;
	
	[Attribute("", UIWidgets.ResourceNamePicker, desc: "HDR material for this color", "emat")]
	protected ResourceName m_Material;

	[Attribute("0 0 0", UIWidgets.EditBox, desc: "Eyecup glow colour on the wearer's face (RGB 0-1). 0 0 0 = automatic from the name / material.")]
	protected vector m_vFaceGlowColor;

	string GetName() { return m_sName; }
	ResourceName GetMaterial() { return m_Material; }
	vector GetFaceGlowColor() { return m_vFaceGlowColor; }
}

[ComponentEditorProps(category: "ACM/Equipment", description: "Night vision goggle functionality")]
class ACM_NightVisionComponentClass : SCR_GadgetComponentClass
{
}

class ACM_NightVisionComponent : SCR_GadgetComponent
{
	// Post-processing materials
	[Attribute("", UIWidgets.ResourceNamePicker, desc: "HDR colorization material for NV effect", "emat", category: "ACM NVG - Visuals")]
	protected ResourceName m_HDRMaterial;

	[Attribute("1", UIWidgets.CheckBox, desc: "Eyecup glow: the phosphor colour lights the wearer's face while the night vision is on (seen by everyone). Flashes when the colour changes.", category: "ACM NVG - Visuals")]
	protected bool m_bFaceGlow;

	[Attribute("0 0 0", UIWidgets.EditBox, desc: "Eyecup glow colour for the default material (RGB 0-1). 0 0 0 = automatic from the material name.", category: "ACM NVG - Visuals")]
	protected vector m_vDefaultFaceGlowColor;

	//! Last glow state sent, so the link is only asked when something changed.
	protected bool m_bFaceGlowSentOn;
	protected vector m_vFaceGlowSentColor;
	protected bool m_bFaceGlowSentAny;

	[Attribute("", UIWidgets.ResourceNamePicker, desc: "Film grain overlay material", "emat", category: "ACM NVG - Visuals")]
	protected ResourceName m_GrainMaterial;
	
	[Attribute("", UIWidgets.ResourceNamePicker, desc: "Optional signal blemish/noise material", "emat", category: "ACM NVG - Visuals")]
	protected ResourceName m_BlemishMaterial;
	
	// Brightness/Gain settings
	// Gain on top of the naked-eye exposure (phosphor materials run with automatic HDR off).
	[Attribute("1.5", UIWidgets.Slider, desc: "Starting brightness: gain over the naked-eye exposure", category: "ACM NVG - Brightness", params: "0.05 20 0.05")]
	protected float m_fBaseBrightness;
	
	[Attribute("0", UIWidgets.CheckBox, desc: "Allow manual brightness adjustment", category: "ACM NVG - Brightness")]
	protected bool m_bManualBrightnessEnabled;
	
	[Attribute("2.0", UIWidgets.Slider, desc: "Brightness adjustment increment", category: "ACM NVG - Brightness", params: "0.25 5 0.25")]
	protected float m_fBrightnessStep;
	
	[Attribute("0.05", UIWidgets.Slider, desc: "Lowest allowed brightness", category: "ACM NVG - Brightness", params: "0.01 2 0.01")]
	protected float m_fMinBrightness;
	
	[Attribute("20", UIWidgets.Slider, desc: "Highest allowed brightness", category: "ACM NVG - Brightness", params: "1 25 0.5")]
	protected float m_fMaxBrightness;
	
	// Animation
	//
	// TEMPORARY. One line per state transition, never per frame. Turn off once the flip is confirmed
	// working - it is here so the log says conclusively which of the two animation paths fired.

	// The signal NVS used, and what every ACM device model is authored against.
	protected static const string ACM_DEFAULT_ROTATOR_SIGNAL = "w_nvRotator";

	// Defaulted rather than left empty.
	//
	// NVS called this attribute m_sSignalName; ACM renamed it to m_sAnimationSignal. A prefab
	// authored against the old name carries no value in the new one, so this read back empty,
	// UpdateAnimation returned on its first line, and the goggles never moved - with no error
	// anywhere. The default plus the runtime fallback in UpdateAnimation covers both halves.
	[Attribute("w_nvRotator", UIWidgets.Auto, desc: "Procedural animation signal identifier. Fallback path, used only when the device has no ACM_AnimationComponent. Leave as w_nvRotator unless the model's ProcAnim uses a different signal.", category: "ACM NVG - Animation")]
	protected string m_sAnimationSignal;
	
	// Phosphor color materials - dynamic array for unlimited colors
	[Attribute("", UIWidgets.Auto, desc: "List of phosphor color options", category: "ACM NVG - Colors")]
	protected ref array<ref ACM_PhosphorColorOption> m_aPhosphorColors;

	// Tube view - night vision only inside the tube circles, normal view outside them.
	// See ACM_NVGTubeView. FULLSCREEN keeps the old whole-screen effect.
	[Attribute("0", UIWidgets.ComboBox, desc: "Tube shape. FULLSCREEN = night vision covers the whole screen (old behaviour). SINGLE / DUAL / PANORAMIC = night vision only inside the tubes, normal view outside. CUSTOM = use Custom Tubes.", category: "ACM NVG - Tubes", enums: ParamEnumArray.FromEnum(ACM_ENVGTubeLayout))]
	protected ACM_ENVGTubeLayout m_eTubeLayout;

	[Attribute("", UIWidgets.Object, desc: "Tube circles used when Tube Layout is CUSTOM. Units are screen heights from the screen centre.", category: "ACM NVG - Tubes")]
	protected ref array<ref ACM_NVGTube> m_aCustomTubes;

	[Attribute("0.08", UIWidgets.Slider, desc: "How soft the tube edge is, as a fraction of the tube radius.", category: "ACM NVG - Tubes", params: "0 0.5 0.01")]
	protected float m_fTubeEdgeSoftness;

	[Attribute("0.85", UIWidgets.Slider, desc: "Render resolution of the night vision picture. Lower = faster; grain hides the softness.", category: "ACM NVG - Tubes", params: "0.3 1 0.05")]
	protected float m_fTubeResolutionScale;

	[Attribute("-0.5", UIWidgets.Slider, desc: "Exposure of the naked-eye surroundings outside the tubes while the goggles are on, in light stops (0 = unchanged, negative = darker).", category: "ACM NVG - Tubes", params: "-4 0 0.1")]
	protected float m_fSurroundEV;

	//! Main camera the surroundings exposure offset was written to (-1 = none).
	protected int m_iSurroundCam = -1;

	[Attribute("0.12", UIWidgets.Slider, desc: "Lens distortion towards the tube edge. 0 = none. Needs the radial tube mask texture.", category: "ACM NVG - Tubes", params: "0 0.5 0.01")]
	protected float m_fTubeEdgeDistortion;

	[Attribute("1", UIWidgets.Slider, desc: "Darkness of the thin faded black outline around each tube. 0 = no outline.", category: "ACM NVG - Tubes", params: "0 1 0.05")]
	protected float m_fTubeRimDarkness;

	protected bool m_bUsingTubeView;
	protected bool m_bVanillaGrainRegistered;

	// Internal references
	protected SignalsManagerComponent m_pSignalMgr;
	protected InventoryItemComponent m_pInventoryItem;
	protected SCR_CharacterControllerComponent m_pCharController;
	
	// State tracking
	protected bool m_bIsWorn;
	protected bool m_bMenuOpen;

	// TEMPORARY. Traces this component's writes to the shared rotator signal.
	protected static const bool ACM_NV_ANIM_DEBUG = true;

	// Set only for the duration of a visor-driven ToggleActive. See ACM_SetVisorPose.
	protected bool m_bVisorPoseOnly;
	protected float m_fCurrentBrightness = -1;
	protected bool m_bInputsRegistered;
	protected int m_iCurrentColorIndex;
	
	// Retry counter for deferred init
	protected int m_iDeferredKitRetryCount;
	protected static const int MAX_DEFERRED_RETRIES = 20;
	
	// Brightness adjustment rate limiting
	protected float m_fBrightnessAdjustCooldown;
	protected static const float BRIGHTNESS_ADJUST_RATE = 0.05;

	// Brightness guard: the manual exposure we set on the tube camera can be dropped back to the
	// engine's automatic exposure by something else (another device's revert, a tube view rebuild).
	// When that happened the picture "auto-corrected" and the brightness keys stopped reaching the
	// camera until the goggles were cycled. The guard re-asserts it a few times a second.
	protected float m_fBrightnessGuardTimer;
	protected int m_iBrightnessGuardLogs;
	protected static const float BRIGHTNESS_GUARD_INTERVAL = 0.25;
	protected static const int BRIGHTNESS_GUARD_MAX_LOGS = 8;

	// Exposure reference. The phosphor HDR materials run with automatic HDR OFF ("HDR 0", like
	// vanilla scopes); with it on, the tube camera re-metered the scene every frame and cancelled
	// the user's brightness ("auto-corrects", keys do nothing). With it off the camera's HDR
	// brightness is the final exposure, so it is the naked-eye exposure (sampled from the main
	// camera when the goggles switch on, or on reset) times the user's brightness as a gain.
	protected float m_fExposureReference = -1;
	//! Exposure factor when the night vision chain runs on the main camera (see GetAppliedExposure).
	protected static const float MAIN_CAMERA_GAIN_SCALE = 0.5;
	
	// State stability - prevent random shutoffs from brief inventory glitches
	protected int m_iNotWornFrameCount;
	protected static const int WORN_DEBOUNCE_FRAMES = 10;
	protected float m_fLastKnownGoodTime;
	protected bool m_bForceStateRecovery;
	// FPS patch: inventory/storage worn-state checks are throttled instead of per-frame.
	protected float m_fWornCheckTimer;
	protected static const float WORN_CHECK_INTERVAL = 0.25;
	
	// Postprocess slot ownership tracking. Only revert what we wrote, and only on
	// the same camera we wrote to. Writing default values to slots we never owned
	// (or to a stale camId after the active camera changed) corrupts another mod's
	// or the engine's own postprocess binding and crashes natively next render.

	protected int m_iAppliedCamId = -1;
	protected bool m_bSlot8Applied;
	protected bool m_bSlot9Applied;
	protected bool m_bSlot11Applied;
	protected bool m_bSlot16Applied;
	protected bool m_bHDRBrightnessApplied;
	
	// Constants
	protected static const ResourceName DEFAULT_HDR = "{9DEECCABE8357209}Common/Postprocess/HDR.emat";
	
	//------------------------------------------------------------------------------------------------
	// Initialization
	//------------------------------------------------------------------------------------------------
	protected override void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);
		
		m_pInventoryItem = InventoryItemComponent.Cast(owner.FindComponent(InventoryItemComponent));
		m_pSignalMgr = SignalsManagerComponent.Cast(owner.FindComponent(SignalsManagerComponent));
		
		if (m_fCurrentBrightness < 0)
			m_fCurrentBrightness = m_fBaseBrightness;
		
		if (GetGame() && GetGame().GetCallqueue())
		{
			GetGame().GetCallqueue().CallLater(DeferredKitLoadCheck, 500, false);

			// Every instance, not just the one being toggled. UpdateAnimation asserts ACTIVE when a
			// toggle happens, but plenty of instances exist that never receive a toggle and still
			// get rendered: the inventory paperdoll and arsenal preview characters (fresh clones in
			// a preview world), and other players' devices. All of them spawn with ACTIVE cleared,
			// so their ProcAnim never evaluates and the rigged mesh sits in its bind pose - goggles
			// deployed across the face. Ensuring the signal exists (stowed) and the entity is active
			// makes every undriven instance settle into the stowed pose instead.
			GetGame().GetCallqueue().CallLater(ACM_EnsureAnimTicking, 250, false);
		}
	}

	//------------------------------------------------------------------------------------------------
	// See EOnInit. Runs once, 250ms after init so the flip user action / detection component have had
	// their chance to create the rotator signal first with their agreed initial value.
	protected void ACM_EnsureAnimTicking()
	{
		IEntity owner = GetOwner();
		if (!owner || !m_pSignalMgr)
			return;

		string signalName = m_sAnimationSignal;
		if (signalName.IsEmpty())
			signalName = ACM_DEFAULT_ROTATOR_SIGNAL;

		// Only CREATE the signal when nobody else has - and creation parameters must match
		// ACM_NV_FlipAction through the shared helper (
		// initial 0 = STOWED after the polarity flip). Never write into an existing signal here: on a live worn device the
		// real state is owned by UpdateAnimation and stomping it from a deferred init would undo a
		// toggle that happened in the first 250ms (stream-in restore does exactly that).
		int sigId = m_pSignalMgr.FindSignal(signalName);
		if (sigId < 0)
			EnsureAnimationSignal(owner, m_pSignalMgr, signalName);

		// The actual point: an inactive entity never ticks its ProcAnimComponent, and preview
		// clones are never toggled, so this is their only chance to leave the bind pose.
		if ((owner.GetFlags() & EntityFlags.ACTIVE) == 0)
			owner.SetFlags(EntityFlags.ACTIVE, false);
	}
	
	//------------------------------------------------------------------------------------------------
	// Deferred check for saved kit loading
	//------------------------------------------------------------------------------------------------
	protected void DeferredKitLoadCheck()
	{
		if (m_bInputsRegistered)
			return;
		
		if (!m_CharacterOwner)
		{
			IEntity localPlayer = SCR_PlayerController.GetLocalControlledEntity();
			if (localPlayer)
			{
				ChimeraCharacter character = ChimeraCharacter.Cast(localPlayer);
				if (character)
				{
					if (IsOwnedByCharacter(character))
						m_CharacterOwner = character;
				}
			}
			
			if (!m_CharacterOwner)
			{
				IEntity current = GetOwner();
				int depth = 0;
				while (current && depth < 15)
				{
					ChimeraCharacter character = ChimeraCharacter.Cast(current);
					if (character)
					{
						m_CharacterOwner = character;
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
				return;
			
			if (GetGame() && GetGame().GetCallqueue())
				GetGame().GetCallqueue().CallLater(DeferredKitLoadCheck, 500, false);
			return;
		}
		
		if (m_CharacterOwner != SCR_PlayerController.GetLocalControlledEntity())
			return;
		
		m_bIsWorn = CheckIsWorn();
		
		if (m_bIsWorn && !m_bInputsRegistered)
		{
			// Activate gadget update for Update() to run (needed for input context and state checks)
			// Update() already checks for local player only, so it's safe
			ActivateGadgetUpdate();
			RegisterInputs();
			m_bInputsRegistered = true;
			
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
	
	override EGadgetType GetType()
	{
		return EGadgetType.NIGHT_VISION;
	}
	
	//------------------------------------------------------------------------------------------------
	// Public accessors
	//------------------------------------------------------------------------------------------------
	float GetBrightness() { return m_fCurrentBrightness; }
	float GetBaseBrightness() { return m_fBaseBrightness; }
	bool IsManualAdjustEnabled() { return m_bManualBrightnessEnabled; }
	bool IsNVGActive() { return m_bActivated; }

	//------------------------------------------------------------------------------------------------
	//! Creates (or finds) the rotator signal with the same parameters for every creator, whichever
	//! component or action initialises first.
	static int EnsureAnimationSignal(IEntity owner, SignalsManagerComponent signals, string signalName)
	{
		if (!owner || !signals)
			return -1;

		return signals.AddOrFindMPSignal(signalName, 0.01, 0.5, 0);
	}
	bool IsNVGWorn() { return m_bIsWorn; }
	
	//------------------------------------------------------------------------------------------------
	// Public toggle methods for radial menu
	//------------------------------------------------------------------------------------------------
	void ToggleNVG()
	{
		// Refresh worn state first
		m_bIsWorn = CheckIsWorn();
		
		if (!m_bIsWorn)
			return;
		
		OnToggleInput();
	}
	
	void ToggleThermal()
	{
		return;
	}
	
	//------------------------------------------------------------------------------------------------
	// Color cycling methods
	//------------------------------------------------------------------------------------------------
	int GetCurrentColorIndex() { return m_iCurrentColorIndex; }
	
	string GetCurrentColorName()
	{
		if (m_iCurrentColorIndex == 0)
			return "Default";
		
		if (m_aPhosphorColors && m_iCurrentColorIndex > 0 && m_iCurrentColorIndex <= m_aPhosphorColors.Count())
			return m_aPhosphorColors[m_iCurrentColorIndex - 1].GetName();
		
		return "Default";
	}
	
	int GetAvailableColorCount()
	{
		if (!m_aPhosphorColors)
			return 1;
		return 1 + m_aPhosphorColors.Count();
	}
	
	array<string> GetAvailableColorNames()
	{
		array<string> names = {};
		names.Insert("Default");
		
		if (m_aPhosphorColors)
		{
			foreach (ACM_PhosphorColorOption option : m_aPhosphorColors)
			{
				if (option)
					names.Insert(option.GetName());
			}
		}
		
		return names;
	}
	
	void CyclePhosphorColor()
	{
		int maxIndex = GetAvailableColorCount() - 1;
		
		if (maxIndex <= 0)
			return;
		
		m_iCurrentColorIndex = (m_iCurrentColorIndex + 1) % (maxIndex + 1);

		if (m_bActivated)
			ApplyVisualEffect(true, true);

		UpdateFaceGlow();
	}
	
	void SetPhosphorColor(int colorIndex)
	{
		int maxIndex = GetAvailableColorCount() - 1;
		if (colorIndex < 0 || colorIndex > maxIndex)
			colorIndex = 0;
		
		m_iCurrentColorIndex = colorIndex;

		if (m_bActivated)
			ApplyVisualEffect(true, true);

		UpdateFaceGlow();
	}
	
	protected ResourceName GetCurrentPhosphorMaterial()
	{
		if (m_iCurrentColorIndex == 0)
			return m_HDRMaterial;
		
		if (m_aPhosphorColors && m_iCurrentColorIndex > 0 && m_iCurrentColorIndex <= m_aPhosphorColors.Count())
			return m_aPhosphorColors[m_iCurrentColorIndex - 1].GetMaterial();
		
		return m_HDRMaterial;
	}

	//------------------------------------------------------------------------------------------------
	// EYECUP FACE GLOW
	//------------------------------------------------------------------------------------------------
	protected vector GetFaceGlowColor()
	{
		if (m_iCurrentColorIndex > 0 && m_aPhosphorColors && m_iCurrentColorIndex <= m_aPhosphorColors.Count())
		{
			ACM_PhosphorColorOption option = m_aPhosphorColors[m_iCurrentColorIndex - 1];
			if (option)
				return ACM_NVGFaceGlow.ColorForPhosphor(option.GetName(), option.GetMaterial(), option.GetFaceGlowColor());
		}

		return ACM_NVGFaceGlow.ColorForPhosphor("", m_HDRMaterial, m_vDefaultFaceGlowColor);
	}

	//------------------------------------------------------------------------------------------------
	//! Local wearer only: tells everyone (through the player link) whether the eyecups glow and in
	//! which colour. Cheap to call often - it only sends when the state changed.
	protected void UpdateFaceGlow(bool forceOff = false)
	{
		if (!m_bFaceGlow || !m_CharacterOwner)
			return;

		if (m_CharacterOwner != SCR_PlayerController.GetLocalControlledEntity())
			return;

		ACM_PlayerLinkComponent link = ACM_PlayerLinkComponent.Cast(m_CharacterOwner.FindComponent(ACM_PlayerLinkComponent));
		if (!link)
			return;

		bool on = !forceOff && m_bActivated && m_bIsWorn;
		vector color = GetFaceGlowColor();

		if (m_bFaceGlowSentAny && on == m_bFaceGlowSentOn && (!on || color == m_vFaceGlowSentColor))
			return;

		if (!link.AskSetFaceGlow(on, color))
			return;

		m_bFaceGlowSentAny = true;
		m_bFaceGlowSentOn = on;
		m_vFaceGlowSentColor = color;
	}

	//------------------------------------------------------------------------------------------------
	// Manual brightness controls
	//------------------------------------------------------------------------------------------------
	protected void IncreaseBrightness()
	{
		if (!m_bActivated)
			return;
		
		if (m_fBrightnessAdjustCooldown > 0)
			return;
		m_fBrightnessAdjustCooldown = BRIGHTNESS_ADJUST_RATE;
		
		ModifyBrightness(true);
	}
	
	protected void DecreaseBrightness()
	{
		if (!m_bActivated)
			return;
		
		if (m_fBrightnessAdjustCooldown > 0)
			return;
		m_fBrightnessAdjustCooldown = BRIGHTNESS_ADJUST_RATE;
		
		ModifyBrightness(false);
	}
	
	protected void ModifyBrightness(bool increase)
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world || !m_bActivated)
			return;
		
		float step = m_fBrightnessStep;
		
		if (!increase)
			step = -step;
		
		float newVal = Math.Clamp(m_fCurrentBrightness + step, m_fMinBrightness, m_fMaxBrightness);
		
		if (newVal != m_fCurrentBrightness)
		{
			m_fCurrentBrightness = newVal;

			// Only update brightness on the camera we already applied to. If the effects were lost,
			// the guard restores them (with this new value) instead of the key silently doing nothing.
			if (m_bHDRBrightnessApplied && IsAppliedCameraLive(world))
				world.SetCameraHDRBrightness(m_iAppliedCamId, GetAppliedExposure());
			else
				EnforceBrightness();
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Exposure written to the camera: naked-eye exposure times the user's brightness (gain).
	protected float GetAppliedExposure()
	{
		// With the chain on the MAIN camera (dark UI surroundings) the same gain came out about twice
		// as bright as on the separate tube camera (flat green at 1.5), so it is halved there; the
		// brightness numbers the player sees mean the same in both modes.
		float gain = m_fCurrentBrightness;
		if (m_bUsingTubeView && ACM_NVGTubeView.RendersOnMainCamera())
			gain *= MAIN_CAMERA_GAIN_SCALE;

		if (m_fExposureReference <= 0)
			return gain;

		return m_fExposureReference * gain;
	}

	//------------------------------------------------------------------------------------------------
	//! Read the exposure the naked eye is adapted to from the main camera (still automatic).
	protected void SampleExposureReference(BaseWorld world, int mainCamId)
	{
		if (!world || mainCamId < 0)
			return;

		// Full-screen mode puts our own manual exposure on the main camera; never read that back.
		if (m_bHDRBrightnessApplied && mainCamId == m_iAppliedCamId)
			return;

		float e = world.GetCameraHDRBrightness(mainCamId);
		if (e > 0)
			m_fExposureReference = e;
	}

	//------------------------------------------------------------------------------------------------
	//! Keep the manual exposure on the camera we drive. Restores the whole effect chain if it was
	//! lost, re-applies the brightness if the camera was put back on automatic exposure.
	protected void EnforceBrightness()
	{
		if (!m_bActivated || !m_bIsWorn || m_bMenuOpen || !m_CharacterOwner)
			return;
		if (m_CharacterOwner != SCR_PlayerController.GetLocalControlledEntity())
			return;

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return;

		// Another device owns the tube view right now; its camera settings are its own business.
		if (m_bUsingTubeView && ACM_NVGTubeView.IsUp() && !ACM_NVGTubeView.IsActiveFor(this))
			return;

		bool tubesLost = m_bUsingTubeView && !ACM_NVGTubeView.IsActiveFor(this);
		if (!m_bHDRBrightnessApplied || !IsAppliedCameraLive(world) || tubesLost)
		{
			LogBrightnessGuard("effects lost (applied=" + m_bHDRBrightnessApplied.ToString() + " cam=" + m_iAppliedCamId.ToString() + " tubesLost=" + tubesLost.ToString() + "), re-applying");
			ApplyVisualEffect(true, true, false);
			return;
		}

		// Relative tolerance: the exposure is now eye exposure x gain and can be large. A brightness
		// key press lands one frame later, so a small lag right after a press is normal.
		float expected = GetAppliedExposure();
		float actual = world.GetCameraHDRBrightness(m_iAppliedCamId);
		if (Math.AbsFloat(actual - expected) > Math.Max(0.01, expected * 0.02))
		{
			LogBrightnessGuard("camera " + m_iAppliedCamId.ToString() + " exposure " + actual.ToString() + " != " + expected.ToString() + " (gain " + m_fCurrentBrightness.ToString() + " x eye " + m_fExposureReference.ToString() + "), re-applying");
			world.SetCameraHDRBrightness(m_iAppliedCamId, expected);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Darkens the naked-eye view around the tubes (the main camera; the tubes are their own camera)
	//! by m_fSurroundEV stops. Re-asserted from the guard, which also follows camera changes.
	protected void ApplySurroundDim()
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return;

		// Only with the separate tube camera: in main-camera mode this would darken the tubes too.
		if (!m_bUsingTubeView || ACM_NVGTubeView.RendersOnMainCamera() || m_fSurroundEV == 0)
		{
			ClearSurroundDim();
			return;
		}

		int cam = world.GetCurrentCameraId();
		if (cam < 0)
			return;

		if (cam != m_iSurroundCam)
			ClearSurroundDim();

		world.AdjustCameraEV(cam, m_fSurroundEV);
		m_iSurroundCam = cam;
	}

	//------------------------------------------------------------------------------------------------
	protected void ClearSurroundDim()
	{
		if (m_iSurroundCam < 0)
			return;

		BaseWorld world = GetGame().GetWorld();
		if (world)
			world.AdjustCameraEV(m_iSurroundCam, 0);
		m_iSurroundCam = -1;
	}

	//------------------------------------------------------------------------------------------------
	protected void LogBrightnessGuard(string msg)
	{
		if (m_iBrightnessGuardLogs >= BRIGHTNESS_GUARD_MAX_LOGS)
			return;
		m_iBrightnessGuardLogs++;
		Print("[ACM NVG] Brightness guard: " + msg, LogLevel.WARNING);
	}
	
	protected void ResetBrightness()
	{
		if (!m_bActivated)
			return;
		
		// Reset = default gain AND re-read the naked-eye exposure (e.g. after walking from a lit
		// room into a dark field).
		m_fCurrentBrightness = m_fBaseBrightness;

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return;

		if (m_bUsingTubeView)
			SampleExposureReference(world, world.GetCurrentCameraId());

		if (m_bHDRBrightnessApplied && IsAppliedCameraLive(world))
			world.SetCameraHDRBrightness(m_iAppliedCamId, GetAppliedExposure());
	}

	//------------------------------------------------------------------------------------------------
	// The tube camera is never the "current" camera, so the old current-camera check would have
	// blocked brightness changes whenever the tube view is in use.
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
		ACM_MPDiag.Log("NV key on " + GetOwner().ToString() + ": menuOpen=" + m_bMenuOpen.ToString() + " owner=" + (m_CharacterOwner != null).ToString() + " ownerIsLocal=" + (m_CharacterOwner && m_CharacterOwner == SCR_PlayerController.GetLocalControlledEntity()).ToString() + " active=" + m_bActivated.ToString());

		if (m_bMenuOpen)
			return;

		if (!m_CharacterOwner)
			return;

		if (m_CharacterOwner != SCR_PlayerController.GetLocalControlledEntity())
			return;

		// Refresh worn state before toggling
		m_bIsWorn = CheckIsWorn();

		if (!m_bIsWorn)
		{
			ACM_MPDiag.Log("NV key ignored: not worn");
			return;
		}

		RplId gadgetId = Replication.FindItemId(this);
		ACM_MPDiag.Log("NV ToggleActive(" + (!m_bActivated).ToString() + ") useMask=" + m_eUseMask.ToString() + " character[" + ACM_MPDiag.Roles(m_CharacterOwner) + "] item[" + ACM_MPDiag.Roles(GetOwner()) + "] gadgetRplIdValid=" + gadgetId.IsValid().ToString() + " gadgetManager=" + (SCR_GadgetManagerComponent.GetGadgetManager(m_CharacterOwner) != null).ToString());
		
		// REQUESTS the toggle, and does nothing else.
		//
		// This used to flip m_bActivated itself, then call ApplyVisualEffect and UpdateAnimation,
		// and THEN call ToggleActive - which routes to OnToggleActive, which does exactly those two
		// things again. One keypress therefore ran the effect and the animation twice, the second
		// pass starting from a state the first had already changed. On the command path that is two
		// CMD_FLIP_ACTION calls per press: a transition fighting itself.
		//
		// OnToggleActive is now the single place state is applied: one input, one state update,
		// one animation update.
		ToggleActive(!m_bActivated, SCR_EUseContext.CUSTOM);
	}
	
	protected void OnThermalToggleInput()
	{
		return;
	}
	
	protected void OnPaletteInput()
	{
		if (!m_bActivated)
			return;
		
		CyclePhosphorColor();
	}
	
	//------------------------------------------------------------------------------------------------
	// HOST / LISTEN SERVER FIX.
	//
	// Vanilla SCR_GadgetComponent.ToggleActive applies the new state locally only when the gadget is a
	// network PROXY (a normal client), and otherwise relies on its "ask the server" RPC to call
	// OnToggleActive. For a player who IS the server (hosting from the game, or the Workbench host),
	// the goggles are the authority copy, so the local apply is skipped and the night vision never
	// switched on. If the vanilla path left the state unchanged and this machine owns the goggles'
	// authority copy for its own character, apply it here.
	//------------------------------------------------------------------------------------------------
	override void ToggleActive(bool state, SCR_EUseContext context)
	{
		bool before = m_bActivated;
		super.ToggleActive(state, context);

		if (m_bActivated != before || state == before)
			return;

		if (!m_CharacterOwner || m_CharacterOwner != SCR_PlayerController.GetLocalControlledEntity())
			return;

		RplComponent itemRpl = RplComponent.Cast(GetOwner().FindComponent(RplComponent));
		if (itemRpl && itemRpl.IsProxy())
			return; // a normal client: vanilla already handled it (or the server will)

		ACM_MPDiag.Log("NV host fallback: applying " + state.ToString() + " locally");
		OnToggleActive(state);
	}

	override void OnToggleActive(bool state)
	{
		ACM_MPDiag.Log("NV OnToggleActive(" + state.ToString() + ") on " + GetOwner().ToString() + " ownerIsLocal=" + (m_CharacterOwner && m_CharacterOwner == SCR_PlayerController.GetLocalControlledEntity()).ToString() + " worn=" + m_bIsWorn.ToString());
		m_bActivated = state;
		ApplyVisualEffect(state);
		UpdateAnimationTo(state, "OnToggleActive");
		UpdateFaceGlow();
	}

	//------------------------------------------------------------------------------------------------
	// LOWER OR RAISE THE OPTIC THROUGH THE GADGET SYSTEM.
	//
	// Writing w_nvRotator directly is provably not enough on an IVAS. The log settled it: same signal
	// id as night vision resolves (id=0 findId=0), value written and read back correct, held steady
	// for two full seconds, entity ACTIVE, ProcAnimComponent present - and the model does not move.
	// Every NVG that DOES move is flipped through ToggleActive instead, so the gadget system, not the
	// signal write, is what actually makes the device animate.
	//
	// So the visor stops writing the signal behind the gadget system's back and asks for the same
	// transition night vision asks for. m_bVisorPoseOnly keeps the power state out of it.
	//------------------------------------------------------------------------------------------------
	void ACM_SetVisorPose(bool down)
	{
		m_bVisorPoseOnly = true;
		ToggleActive(down, SCR_EUseContext.CUSTOM);
		m_bVisorPoseOnly = false;
	}
	
	//------------------------------------------------------------------------------------------------
	// Visual effects
	//------------------------------------------------------------------------------------------------
	//------------------------------------------------------------------------------------------------
	protected void DisableVanillaNightGrain(bool disable)
	{
		// Balanced per instance. Re-applying while already on (colour change, menu close) used to
		// register again each time, so the count never returned to zero and the vanilla night
		// noise stayed off after the goggles were removed.
		if (disable == m_bVanillaGrainRegistered)
			return;
		m_bVanillaGrainRegistered = disable;

		// Use global state manager to prevent conflicts when multiple players have NVGs
		if (disable)
			ACM_NVGGlobalStateManager.RegisterActiveNVG();
		else
			ACM_NVGGlobalStateManager.UnregisterActiveNVG();
	}
	
	// Revert ONLY the postprocess slots/HDR brightness we previously wrote, and
	// only on the camera we wrote them to. Safe to call when nothing is applied.
	protected void RevertAppliedEffects()
	{
		ClearSurroundDim();

		if (m_iAppliedCamId < 0)
		{
			m_bSlot8Applied = false;
			m_bSlot9Applied = false;
			m_bSlot11Applied = false;
			m_bSlot16Applied = false;
			m_bHDRBrightnessApplied = false;
			return;
		}
		
		BaseWorld world = GetGame().GetWorld();
		if (world)
		{
			int camId = m_iAppliedCamId;
			
			if (m_bSlot11Applied)
				world.SetCameraPostProcessEffect(camId, 11, PostProcessEffectType.ThermalImaging, "");
			if (m_bSlot16Applied)
				world.SetCameraPostProcessEffect(camId, 16, PostProcessEffectType.HDR, DEFAULT_HDR);
			if (m_bSlot9Applied)
				world.SetCameraPostProcessEffect(camId, 9, PostProcessEffectType.None, "");
			if (m_bSlot8Applied)
				world.SetCameraPostProcessEffect(camId, 8, PostProcessEffectType.None, "");
			if (m_bHDRBrightnessApplied)
				world.SetCameraHDRBrightness(camId, -1);
		}
		
		m_bSlot8Applied = false;
		m_bSlot9Applied = false;
		m_bSlot11Applied = false;
		m_bSlot16Applied = false;
		m_bHDRBrightnessApplied = false;
		m_iAppliedCamId = -1;
	}
	
	void ApplyVisualEffect(bool enable, bool force = false, bool touchDetection = true)
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
		if (isLocalPlayer)
			ACM_MPDiag.Log("NV ApplyVisualEffect(" + enable.ToString() + ") camId=" + camId.ToString() + " worn=" + m_bIsWorn.ToString() + " layout=" + typename.EnumToString(ACM_ENVGTubeLayout, m_eTubeLayout));
		if (camId < 0)
			return;

		if (enable && m_bIsWorn)
		{
			// TUBE VIEW: the chain goes on the tube camera and the main camera stays clean, so
			// only the inside of the tubes is night vision. Falls back to the main camera (the
			// old full-screen effect) if the view cannot be built.
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

			// Active camera changed since last apply -> revert old camera first to
			// avoid leaving slots bound on a camera we no longer track.
			if (m_iAppliedCamId >= 0 && m_iAppliedCamId != camId)
				RevertAppliedEffects();

			m_bUsingTubeView = useTubes;

			if (isLocalPlayer)
				SetDeviceVisible(false);

			// Vanilla night noise is the naked-eye view. With tubes, that is what surrounds them,
			// so it stays on; only the full-screen effect replaces it.
			DisableVanillaNightGrain(!useTubes || ACM_NVGTubeView.RendersOnMainCamera());

			m_iAppliedCamId = camId;

			world.SetCameraPostProcessEffect(camId, 11, PostProcessEffectType.ThermalImaging, "");
			
			if (!m_GrainMaterial.IsEmpty())
			{
				world.SetCameraPostProcessEffect(camId, 9, PostProcessEffectType.FilmGrain, m_GrainMaterial);
				m_bSlot9Applied = true;
			}
			
			if (!m_BlemishMaterial.IsEmpty())
			{
				world.SetCameraPostProcessEffect(camId, 8, PostProcessEffectType.FilmGrain, m_BlemishMaterial);
				m_bSlot8Applied = true;
			}
			
			ResourceName hdrMat = GetCurrentPhosphorMaterial();
			if (!hdrMat.IsEmpty())
			{
				world.SetCameraPostProcessEffect(camId, 16, PostProcessEffectType.HDR, hdrMat);
				m_bSlot16Applied = true;
			}
			
			if (m_fCurrentBrightness < 0 || m_fCurrentBrightness < m_fMinBrightness)
				m_fCurrentBrightness = m_fBaseBrightness;

			// First apply of a session: take the naked-eye exposure as the reference. Later
			// re-applies (colour change, menu close, guard) keep it, so brightness stays put.
			if (m_fExposureReference <= 0)
				SampleExposureReference(world, world.GetCurrentCameraId());

			world.SetCameraHDRBrightness(camId, GetAppliedExposure());
			m_bHDRBrightnessApplied = true;

			// The naked-eye surroundings around the tubes use the same eye exposure (locked), so they
			// look like the night, not brightened to daylight by their own automatic exposure.
			if (m_bUsingTubeView)
				ACM_NVGTubeView.SetPeripheryExposure(m_fExposureReference);

			// Surroundings one shade darker (tube-camera mode only).
			if (isLocalPlayer)
				ApplySurroundDim();
			
			if (m_bManualBrightnessEnabled)
			{
				// AddActionListener does not deduplicate, and this runs on every re-apply (colour
				// change, menu close, brightness guard), so drop any earlier registration first.
				InputManager im = GetGame().GetInputManager();
				im.RemoveActionListener("ACM_NV_Increase", EActionTrigger.PRESSED, IncreaseBrightness);
				im.RemoveActionListener("ACM_NV_Decrease", EActionTrigger.PRESSED, DecreaseBrightness);
				im.RemoveActionListener("ACM_NV_Reset", EActionTrigger.DOWN, ResetBrightness);
				im.AddActionListener("ACM_NV_Increase", EActionTrigger.PRESSED, IncreaseBrightness);
				im.AddActionListener("ACM_NV_Decrease", EActionTrigger.PRESSED, DecreaseBrightness);
				im.AddActionListener("ACM_NV_Reset", EActionTrigger.DOWN, ResetBrightness);
			}
		}
		else
		{
			if (isLocalPlayer)
				SetDeviceVisible(true);

			RevertAppliedEffects();
			ReleaseTubeView();

			// Switched off for real (not just a menu): read the eye exposure afresh next time.
			if (!m_bActivated)
				m_fExposureReference = -1;

			DisableVanillaNightGrain(false);
			
			if (m_bManualBrightnessEnabled)
			{
				InputManager im = GetGame().GetInputManager();
				im.RemoveActionListener("ACM_NV_Increase", EActionTrigger.PRESSED, IncreaseBrightness);
				im.RemoveActionListener("ACM_NV_Decrease", EActionTrigger.PRESSED, DecreaseBrightness);
				im.RemoveActionListener("ACM_NV_Reset", EActionTrigger.DOWN, ResetBrightness);
			}
			
		}
		
		ACM_IRSystem ir = ACM_IRSystem.GetInstance();
		if (ir)
			ir.SetIRVisibility(enable);
	}

	//------------------------------------------------------------------------------------------------
	// Thermal gadget driving.
	//
	// ACM_ThermalComponentV2 can live on this same entity, and when it does it reports the
	// same EGadgetType as we do. The gadget manager routes mode changes to only the first
	// gadget of a type - this one - so the thermal component receives ModeClear but never
	// ModeSwitch. No ModeSwitch means no ActivateGadgetUpdate, so no Update, so it never
	// activates its input context, and an action in a context that was never activated
	// reads zero. That is indistinguishable from an unbound key, which is why the thermal
	// keybinds did nothing while the radial menu (which calls ToggleThermal directly) kept
	// working.
	//
	// Forwarding our own lifecycle is the fix: we are the component the manager actually
	// drives, so we hand the thermal component the events it should have received. It
	// ignores anything forwarded here once it is driven directly, so a thermal-only device
	// with no NVG component still works on its own.
	//------------------------------------------------------------------------------------------------
	protected ACM_ThermalComponentV2 GetThermalComponent()
	{
		IEntity owner = GetOwner();
		if (!owner)
			return null;
		
		return ACM_ThermalComponentV2.Cast(owner.FindComponent(ACM_ThermalComponentV2));
	}
	
	protected void SetDeviceVisible(bool visible)
	{
	}
	
	//------------------------------------------------------------------------------------------------
	// Animation
	//------------------------------------------------------------------------------------------------
	// TWO PATHS, IN PRIORITY ORDER - restored from NVS.
	//
	//   1. ACM_AnimationComponent, driving the CMD_FLIP_ACTION animation command. The real path for
	//      a device with an authored animation graph and flip clips.
	//   2. The procedural signal, rotating a bone directly. The fallback.
	//
	// ACM had lost path 1 entirely and kept only path 2, so a device authored for the command had
	// nothing driving it. This restores the original priority without removing the fallback.
	//
	protected void UpdateAnimation()
	{
		UpdateAnimationTo(m_bActivated, "wrapper");
	}

	//------------------------------------------------------------------------------------------------
	// The same animation path with an EXPLICIT pose, so the IVAS visor can drive it without having to
	// pretend night vision was switched on. UpdateAnimation() is the ordinary caller and passes its
	// own activated state, which is exactly what it did before this was split.
	//------------------------------------------------------------------------------------------------
	protected void UpdateAnimationTo(bool poseDown, string via = "?")
	{
		// Deferred from RplLoad on every client for every remote device; the owner can be gone by then.
		if (!GetOwner())
			return;

		// Path 1: animation command. Absent on the current device set, which uses ProcAnim.
		ACM_AnimationComponent animComp = ACM_AnimationComponent.Cast(GetOwner().FindComponent(ACM_AnimationComponent));
		if (animComp)
		{
			animComp.FlipNVAction(poseDown, true);
			return;
		}

		// Path 2: procedural signal. This is the live path.
		if (!m_pSignalMgr)
		{
			Print("[ACM NVG] " + GetOwner().ToString() + " has no animation component and no SignalsManagerComponent - the goggles cannot move.", LogLevel.WARNING);
			return;
		}


		// Backward compatibility with prefabs authored against the old NVS attribute name.
		string signalName = m_sAnimationSignal;
		if (signalName.IsEmpty())
			signalName = ACM_DEFAULT_ROTATOR_SIGNAL;

		int sigId = m_pSignalMgr.FindSignal(signalName);
		if (sigId < 0)
		{
			Print("[ACM NVG] signal not found: '" + signalName + "' on " + GetOwner().ToString() + ". The ProcAnim .siga input name and m_sAnimationSignal must match.", LogLevel.WARNING);
			return;
		}

		// THE ACTUAL BUG, proven by the probe: while a device is worn/slotted the storage system
		// leaves the item entity with its ACTIVE flag cleared (probe logged active=false with the
		// character as parent). An inactive entity is never simulated, so its ProcAnimComponent
		// never re-evaluates - the signal value changes underneath it and the rotator bone stays
		// wherever spawn left it. The write below was always landing; nothing was ever consuming it.
		//
		// Re-asserting ACTIVE is idempotent, costs nothing, and this path runs on state transitions
		// only. It is asserted here, immediately before the write, rather than once on mode switch,
		// because the storage system clears it again on re-slotting and inventory shuffles.
		IEntity animOwner = GetOwner();
		if (animOwner && (animOwner.GetFlags() & EntityFlags.ACTIVE) == 0)
		{
			animOwner.SetFlags(EntityFlags.ACTIVE, false);

		}

		// POLARITY FLIPPED with the bind-pose re-rig. Read this before changing either half.
		//
		// The rigged models used to be authored with the goggles DEPLOYED at rest, so rest was
		// deployed and the signal rotated -145 to stow. That made previews - which render the raw
		// bind pose because no proc anim runs in them - show goggles across the character's face.
		//
		// The models were re-rigged so STOWED is the rest pose, and ACM_V2_NVG.siga now multiplies by
		// +145 to swing them DOWN. So the meaning of the signal inverted:
		//
		//   0 = stowed   (bone at rest, 0 degrees)  <- also what an undriven preview shows
		//   1 = deployed (bone at +145 degrees)
		//
		// ACTIVE therefore writes 1, not 0. If the goggles ever flip the wrong way again, check
		// that this line, the sign in ACM_V2_NVG.siga, and the models' rest pose all agree.
		bool value = poseDown;


		// TEMPORARY. On an IVAS this component and the visor share w_nvRotator, so this line is a
		// candidate for overwriting a lowered visor with night vision's own off state.
		if (ACM_NV_ANIM_DEBUG)
		{
			string nv = "[ACM VISOR] NVG writes ";
			nv += value.ToString();
			nv += " via ";
			nv += via;
			nv += " activated=";
			nv += m_bActivated.ToString();
			nv += " worn=";
			nv += m_bIsWorn.ToString();
			nv += " to ";
			nv += signalName;
			nv += " on ";
			nv += GetOwner().ToString();
			Print(nv, LogLevel.DEBUG);
		}

		m_pSignalMgr.SetSignalValue(sigId, value);

	}

	//------------------------------------------------------------------------------------------------
	// Update loop
	//------------------------------------------------------------------------------------------------
	override void Update(float timeSlice)
	{
		super.Update(timeSlice);
		
		// Drive the thermal component before our own early exits - it has its own guards,
		// and skipping it whenever WE have nothing to do is exactly the bug this fixes.
		ACM_ThermalComponentV2 thermal = GetThermalComponent();
		if (thermal)
			thermal.HostUpdate(timeSlice);

		// Early exit for non-local players
		if (!m_CharacterOwner || m_CharacterOwner != SCR_PlayerController.GetLocalControlledEntity())
			return;

		// Keeps the eyecup glow in step with power / worn state (sends only on change).
		UpdateFaceGlow();

		if (m_fBrightnessAdjustCooldown > 0)
			m_fBrightnessAdjustCooldown -= timeSlice;
		
		bool wasWorn = m_bIsWorn;
		m_fWornCheckTimer += timeSlice;
		if (m_fWornCheckTimer >= WORN_CHECK_INTERVAL)
		{
			m_fWornCheckTimer = 0;
			m_bIsWorn = CheckIsWorn();
		}
		
		// Handle worn state changes
		if (m_bIsWorn != wasWorn)
		{
			if (m_bIsWorn)
			{
				// Just became worn
				m_iNotWornFrameCount = 0;
				if (!m_bInputsRegistered)
				{
					RegisterInputs();
					m_bInputsRegistered = true;
					
					if (!m_pCharController)
						m_pCharController = SCR_CharacterControllerComponent.Cast(m_CharacterOwner.GetCharacterController());
					if (m_pCharController)
						m_pCharController.m_OnPlayerDeath.Insert(OnDeath);
				}
				
				// State recovery if needed
				if (m_bForceStateRecovery)
				{
					m_bForceStateRecovery = false;
					m_bActivated = true;
					ApplyVisualEffect(true);
					UpdateAnimationTo(m_bActivated, "Update-recovery");
				}
			}
			else
			{
				// Just became unworn - start debounce
				m_iNotWornFrameCount = 1;
			}
		}
		
		// Handle debounce for unworn state
		if (!m_bIsWorn)
		{
			if (m_iNotWornFrameCount > 0 && m_iNotWornFrameCount < WORN_DEBOUNCE_FRAMES)
			{
				m_iNotWornFrameCount++;
				m_bIsWorn = true; // Keep on during debounce
			}
			else if (m_iNotWornFrameCount >= WORN_DEBOUNCE_FRAMES)
			{
				// Debounce complete - turn off
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
		
		// Track last known good time
		if (m_bActivated)
			m_fLastKnownGoodTime = System.GetTickCount() / 1000.0;
		
		GetGame().GetInputManager().ActivateContext("ACM_NVContext");
		
		// Handle menu state changes
		bool menuNowOpen = GetGame().GetMenuManager().IsAnyMenuOpen();
		if (menuNowOpen != m_bMenuOpen)
		{
			m_bMenuOpen = menuNowOpen;
			// Post-process only. Detection is deliberately left alone - see the touchDetection
			// note in ApplyVisualEffect. Tearing the detection system down here, and trusting
			// something else to raise it on the way back out, is the whole of the menu bug.
			ApplyVisualEffect(!m_bMenuOpen && m_bActivated, true, false);
		}

		// Brightness guard (see m_fBrightnessGuardTimer).
		if (m_bActivated && !m_bMenuOpen)
		{
			m_fBrightnessGuardTimer += timeSlice;
			if (m_fBrightnessGuardTimer >= BRIGHTNESS_GUARD_INTERVAL)
			{
				m_fBrightnessGuardTimer = 0;
				EnforceBrightness();
				ApplySurroundDim();
			}
		}
	}
	
	//------------------------------------------------------------------------------------------------
	// Check if NVG is currently worn - optimized version
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
	
	// Helper: Check if entity is a child of target (optimized)
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
	
	// Helper: Check if item is in storage chain leading to target (consolidated logic)
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
	
	protected void ClearEquipment()
	{
		ModeClear(EGadgetMode.LAST);
		
		if (m_bActivated)
		{
			m_bActivated = false;
			ApplyVisualEffect(false, true);
		}
	}
	
	//------------------------------------------------------------------------------------------------
	// Equipment state changes
	//------------------------------------------------------------------------------------------------
	override protected void ModeSwitch(EGadgetMode mode, IEntity charOwner)
	{
		super.ModeSwitch(mode, charOwner);
		ACM_MPDiag.Log("NV ModeSwitch mode=" + typename.EnumToString(EGadgetMode, mode) + " on " + GetOwner().ToString() + " charOwner=" + (charOwner != null).ToString() + " charIsLocal=" + (charOwner && charOwner == SCR_PlayerController.GetLocalControlledEntity()).ToString());

		// Forwarded first, and unconditionally, because the ON_GROUND/LAST branch below
		// returns early - and that is precisely the case where thermal most needs to hear
		// about it, so it can tear its own camera effects down.
		ACM_ThermalComponentV2 thermal = GetThermalComponent();
		if (thermal)
			thermal.HostModeSwitch(mode, charOwner);

		if (mode == EGadgetMode.ON_GROUND || mode == EGadgetMode.LAST)
		{
			// Check if this is a spurious mode switch (NVG was recently active)
			float currentTime = System.GetTickCount() / 1000.0;
			if (m_bActivated && (currentTime - m_fLastKnownGoodTime) < 2.0)
				m_bForceStateRecovery = true;
			
			// Only clear if we're actually being removed (not just inventory glitch)
			// Set worn state first so ClearNVGEffects can check it
			m_bIsWorn = false;
			UpdateFaceGlow(true);
			
			if (m_bActivated)
			{
				m_bActivated = false;
				ClearNVGEffects();
			}
			
			m_bInputsRegistered = false;
			m_iNotWornFrameCount = 0;
			UnregisterInputs();
			return;
		}
		
		if (!m_CharacterOwner)
			return;
		
		// Activate gadget update for Update() to run (needed for input context and state checks)
		// Update() already checks for local player only, so it's safe
		ActivateGadgetUpdate();
		
		bool wasWorn = m_bIsWorn;
		m_bIsWorn = CheckIsWorn();
		
		bool isLocalPlayer = (m_CharacterOwner == SCR_PlayerController.GetLocalControlledEntity());
		
		UpdateAnimationTo(m_bActivated, "ModeSwitch");
		
		if (m_pInventoryItem)
			m_pInventoryItem.ActivateOwner(true);
		
		m_pCharController = SCR_CharacterControllerComponent.Cast(m_CharacterOwner.GetCharacterController());
		
		if (isLocalPlayer && m_bIsWorn)
		{
			RegisterInputs();
			m_bInputsRegistered = true;
			
			if (m_pCharController)
				m_pCharController.m_OnPlayerDeath.Insert(OnDeath);
		}
		else if (isLocalPlayer && !m_bIsWorn && wasWorn)
		{
			// Only clear if we actually transitioned from worn to unworn
			// This prevents spurious clears during inventory operations
			UnregisterInputs();
			m_bInputsRegistered = false;
			
			if (m_bActivated)
			{
				m_bActivated = false;
				ClearNVGEffects();
			}
		}
	}
	
	protected void ClearNVGEffects()
	{
		// Only clear if we're the local player and actually had NVG active
		// This prevents clearing when other mods' NVGs are active
		if (!m_CharacterOwner || m_CharacterOwner != SCR_PlayerController.GetLocalControlledEntity())
			return;
		
		// Don't clear if still worn - this prevents clears during brief inventory glitches
		// Only clear when we're actually being removed/unworn
		if (m_bIsWorn)
			return;
		
		// Note: callers (ModeSwitch ON_GROUND/LAST, worn->unworn transition) set
		// m_bActivated = false BEFORE invoking us, so we cannot gate on it here.
		// RevertAppliedEffects() is a no-op if we never applied anything, which
		// makes this safe to call unconditionally.
		RevertAppliedEffects();
		ReleaseTubeView();

		DisableVanillaNightGrain(false);

		if (m_bManualBrightnessEnabled)
		{
			InputManager im = GetGame().GetInputManager();
			im.RemoveActionListener("ACM_NV_Increase", EActionTrigger.PRESSED, IncreaseBrightness);
			im.RemoveActionListener("ACM_NV_Decrease", EActionTrigger.PRESSED, DecreaseBrightness);
			im.RemoveActionListener("ACM_NV_Reset", EActionTrigger.DOWN, ResetBrightness);
		}
		
		ACM_IRSystem ir = ACM_IRSystem.GetInstance();
		if (ir)
			ir.SetIRVisibility(false);
		
	}
	
	// THE DEFAULT HERE IS SHOW, AND THAT IS THE WHOLE POINT.
	//
	// This method is the only thing in the mod that hides a device outright, so its failure mode
	// decides how bad a resolution bug can get. It used to fall through to HideOwner() whenever it
	// could not establish that the device was worn - which meant one unresolved owner turned into
	// "nobody can see my NVGs", a bug that reads as broken replication and sent this project looking
	// at the wrong layer more than once.
	//
	// Hiding is now reserved for the case it actually exists for: a device positively known to be
	// stowed inside a container on a character we positively resolved. Every uncertain case shows.
	// A device wrongly shown is a cosmetic oddity somebody notices; a device wrongly hidden is
	// invisible gear with no symptom to search for.
	//------------------------------------------------------------------------------------------------
	override void UpdateVisibility(EGadgetMode mode)
	{
		if (!m_pInventoryItem)
			return;

		if (mode == EGadgetMode.ON_GROUND)
		{
			m_pInventoryItem.ShowOwner();
			return;
		}

		if (CheckIsWorn())
		{
			m_pInventoryItem.ShowOwner();
			return;
		}

		// In no container at all - held, in a preview, or mounted on something loose.
		if (!m_pInventoryItem.GetParentSlot())
		{
			m_pInventoryItem.ShowOwner();
			return;
		}

		// Could not work out who is carrying it. Defence in depth against exactly the failure this
		// method used to have: with no owner resolved, "not worn" is not a fact, it is an absence of
		// information, and hiding on an absence of information is what caused the bug.
		if (!m_CharacterOwner)
		{
			m_pInventoryItem.ShowOwner();
			return;
		}

		// Stowed in a container on a known character. The one case that earns a hide.
		m_pInventoryItem.HideOwner();
	}
	
	override void ModeClear(EGadgetMode mode)
	{
		UnregisterInputs();
		m_bInputsRegistered = false;
		DeactivateGadgetUpdate();
		
		if (m_CharacterOwner && m_pCharController)
			m_pCharController.m_OnPlayerDeath.Remove(OnDeath);
		
		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().Remove(DeferredKitLoadCheck);
	}
	
	//------------------------------------------------------------------------------------------------
	// Input registration
	//------------------------------------------------------------------------------------------------
	protected void RegisterInputs()
	{
		InputManager im = GetGame().GetInputManager();
		if (!im)
			return;

		ACM_MPDiag.Log("NV keys registered on " + GetOwner().ToString());
		im.AddActionListener("ACM_NV_Toggle", EActionTrigger.DOWN, OnToggleInput);
		
		if (m_aPhosphorColors && m_aPhosphorColors.Count() > 0)
			im.AddActionListener("ACM_NV_Palette", EActionTrigger.DOWN, OnPaletteInput);
	}
	
	protected void UnregisterInputs()
	{
		InputManager im = GetGame().GetInputManager();
		ACM_MPDiag.Log("NV keys unregistered on " + GetOwner().ToString());
		im.RemoveActionListener("ACM_NV_Toggle", EActionTrigger.DOWN, OnToggleInput);
		
		if (m_aPhosphorColors && m_aPhosphorColors.Count() > 0)
			im.RemoveActionListener("ACM_NV_Palette", EActionTrigger.DOWN, OnPaletteInput);
	}
	
	//------------------------------------------------------------------------------------------------
	// Events
	//------------------------------------------------------------------------------------------------
	protected void OnDeath()
	{
		UpdateFaceGlow(true);
		m_bMenuOpen = false;
		m_bIsWorn = false;
		m_iNotWornFrameCount = 0;
		m_bForceStateRecovery = false;
		
		ModeClear(EGadgetMode.LAST);
		
		if (m_bActivated)
		{
			m_bActivated = false;
			ApplyVisualEffect(false, true);
		}
	}
	
	//------------------------------------------------------------------------------------------------
	// Replication
	//------------------------------------------------------------------------------------------------
	// NOTE: NVG on/off state is CLIENT-SIDE ONLY (purely visual camera effects)
	// No need to replicate to server - this saves bandwidth and prevents server tick overhead
	// Animation state is handled by base class if needed for visual representation
	protected override bool RplSave(ScriptBitWriter writer)
	{
		// Don't save activation state - it's client-side only
		// Only call super for base class functionality (if any)
		return super.RplSave(writer);
	}

	protected override bool RplLoad(ScriptBitReader reader)
	{
		// Don't load activation state - it's client-side only
		//
		// Animation refresh only. The 300ms delay is the single change here and it is
		// animation-specific: on stream-in the entity is still being assembled and the skeleton the
		// rotator bone lives on is not necessarily bound yet, so refreshing on the next frame writes
		// into something that is not ready and silently does nothing. NVS waits 300ms at this exact
		// point for the same reason. This previously used Call(), i.e. next frame.
		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().CallLater(UpdateAnimation, 300, false);

		return super.RplLoad(reader);
	}

	//------------------------------------------------------------------------------------------------
	// Clear every deferred call this component schedules. Without this, timers armed from
	// RplLoad/OnPostInit on remote proxies can fire after the device entity is deleted.
	override void OnDelete(IEntity owner)
	{
		if (GetGame() && GetGame().GetCallqueue())
		{
			GetGame().GetCallqueue().Remove(UpdateAnimation);
			GetGame().GetCallqueue().Remove(ACM_EnsureAnimTicking);
			GetGame().GetCallqueue().Remove(DeferredKitLoadCheck);
		}

		UpdateFaceGlow(true);

		// Deleted while switched on (respawn, item destroyed): take the tube view and the
		// vanilla-noise suppression down with it.
		if (m_bUsingTubeView)
			RevertAppliedEffects();
		ReleaseTubeView();
		DisableVanillaNightGrain(false);

		super.OnDelete(owner);
	}

}
