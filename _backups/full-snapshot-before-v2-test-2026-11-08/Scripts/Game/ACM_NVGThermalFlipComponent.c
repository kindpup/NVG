[ComponentEditorProps(category: "GameScripted/Gadgets", description: "Two-mode NVG and thermal view controller.")]
class ACM_NVGThermalFlipComponentClass : ScriptComponentClass
{
}

// Local-only post-process controller. ACM_ToggleNVG powers the view; ACM_ChangeSightMode
// switches between green night vision and white-hot thermal.
//
// The picture is shown through ACM_NVGTubeView: night vision / thermal only inside the tubes,
// the normal world outside them. Set Tube Layout to FULLSCREEN for the old whole-screen effect.
class ACM_NVGThermalFlipComponent : ScriptComponent
{
	[Attribute("ACM_ToggleNVG", UIWidgets.EditBox, "Action that toggles the NVG view on/off.")]
	protected string m_sPowerAction;

	[Attribute("ACM_ChangeSightMode", UIWidgets.EditBox, "Action that switches between night vision and thermal.")]
	protected string m_sModeAction;

	[Attribute("{9A4F5D6C7E8F1023}UI/Materials/ACM_NVG_GreenPhosphor.emat", UIWidgets.ResourceNamePicker, "Green phosphor HDR material.", "emat")]
	protected ResourceName m_sNightVisionMaterial;

	[Attribute("{8B3E4C5D6A7F9012}UI/Materials/ACM_Thermal_WhiteHot.emat", UIWidgets.ResourceNamePicker, "White-hot thermal imaging material.", "emat")]
	protected ResourceName m_sThermalMaterial;

	[Attribute("2", UIWidgets.ComboBox, "Tube shape. FULLSCREEN = whole screen (old behaviour).", enums: ParamEnumArray.FromEnum(ACM_ENVGTubeLayout), category: "Tubes")]
	protected ACM_ENVGTubeLayout m_eTubeLayout;

	[Attribute("", UIWidgets.Object, "Tube circles used when Tube Layout is CUSTOM. Units are screen heights from the screen centre.", category: "Tubes")]
	protected ref array<ref ACM_NVGTube> m_aCustomTubes;

	[Attribute("0.08", UIWidgets.Slider, "How soft the tube edge is, as a fraction of the tube radius.", params: "0 0.5 0.01", category: "Tubes")]
	protected float m_fTubeEdgeSoftness;

	[Attribute("1", UIWidgets.Slider, "Render resolution of the night vision / thermal picture. Lower values are cheaper but blurrier.", params: "0.3 1 0.05", category: "Tubes")]
	protected float m_fTubeResolutionScale;

	[Attribute("0.12", UIWidgets.Slider, "Lens distortion towards the tube edge. 0 = none. Needs the radial tube mask texture.", params: "0 0.5 0.01", category: "Tubes")]
	protected float m_fTubeEdgeDistortion;

	[Attribute("1", UIWidgets.Slider, "Darkness of the thin faded black outline around each tube. 0 = no outline.", params: "0 1 0.05", category: "Tubes")]
	protected float m_fTubeRimDarkness;

	protected static const int ACM_PP_THERMAL_SLOT = 11;
	protected static const int ACM_PP_HDR_SLOT = 16;
	protected static const ResourceName ACM_DEFAULT_HDR = "{9DEECCABE8357209}Common/Postprocess/HDR.emat";

	protected bool m_bNVGActive;
	protected int m_iMode;	// 0 = green night vision, 1 = white-hot thermal
	protected int m_iAppliedCamera = -1;
	protected int m_iAppliedMode = -1;
	protected bool m_bEffectApplied;
	protected bool m_bUsingTubeView;
	protected BaseWorld m_World;
	protected InputManager m_RegisteredInput;

	void ACM_NVGThermalFlipComponent(IEntityComponentSource src, IEntity ent, IEntity parent)
	{
		SetEventMask(ent, EntityEvent.FRAME | EntityEvent.INIT);
	}

	override void EOnInit(IEntity owner)
	{
		m_World = owner.GetWorld();
		m_bNVGActive = false;
		m_iMode = 0;
		EnsureInputListeners();
	}

	protected void EnsureInputListeners()
	{
		if (m_RegisteredInput || !GetGame())
			return;

		InputManager input = GetGame().GetInputManager();
		if (!input)
			return;

		input.AddActionListener(m_sPowerAction, EActionTrigger.DOWN, OnPowerAction);
		input.AddActionListener(m_sModeAction, EActionTrigger.DOWN, OnModeAction);
		m_RegisteredInput = input;
	}

	protected bool IsLocalWearer(IEntity owner)
	{
		if (!owner || !GetGame())
			return false;

		PlayerController controller = GetGame().GetPlayerController();
		if (!controller)
			return false;

		IEntity character = controller.GetControlledEntity();
		if (!character)
			return false;

		InventoryItemComponent ownerItem = InventoryItemComponent.Cast(owner.FindComponent(InventoryItemComponent));
		if (ownerItem)
		{
			SCR_InventoryStorageManagerComponent storageManager = SCR_InventoryStorageManagerComponent.Cast(character.FindComponent(SCR_InventoryStorageManagerComponent));
			if (storageManager)
			{
				SCR_CharacterInventoryStorageComponent characterStorage = storageManager.GetCharacterStorage();
				if (characterStorage)
				{
					InventoryItemComponent headgear = characterStorage.GetItemFromLoadoutSlot(new LoadoutHeadCoverArea());
					if (headgear == ownerItem)
						return true;
				}
			}
		}

		IEntity current = owner;
		while (current)
		{
			if (current == character)
				return true;
			current = current.GetParent();
		}
		return false;
	}

	protected void OnPowerAction()
	{
		IEntity owner = GetOwner();
		if (!owner || !IsLocalWearer(owner))
			return;

		m_bNVGActive = !m_bNVGActive;
		if (!m_bNVGActive)
		{
			ClearAppliedEffect();
			return;
		}

		ApplyCurrentMode();
	}

	protected void OnModeAction()
	{
		IEntity owner = GetOwner();
		if (!owner || !m_bNVGActive || !IsLocalWearer(owner))
			return;

		m_iMode = 1 - m_iMode;
		ApplyCurrentMode();
	}

	//------------------------------------------------------------------------------------------------
	//! Picks the camera the effect goes on: the tube camera when the tube view is available,
	//! otherwise the main camera (full-screen fallback).
	protected int ResolveTargetCamera()
	{
		m_bUsingTubeView = false;

		if (m_eTubeLayout != ACM_ENVGTubeLayout.FULLSCREEN)
		{
			if (ACM_NVGTubeView.Acquire(this, m_eTubeLayout, m_aCustomTubes, m_fTubeEdgeSoftness, m_fTubeResolutionScale, m_fTubeEdgeDistortion, m_fTubeRimDarkness))
			{
				m_bUsingTubeView = true;
				return ACM_NVGTubeView.CAMERA_INDEX;
			}
		}

		ACM_NVGTubeView.Release(this);
		return m_World.GetCurrentCameraId();
	}

	protected void ApplyCurrentMode()
	{
		if (!m_World || !m_bNVGActive)
			return;

		IEntity owner = GetOwner();
		if (!owner || !IsLocalWearer(owner))
		{
			m_bNVGActive = false;
			ClearAppliedEffect();
			return;
		}

		int cameraId = ResolveTargetCamera();
		if (cameraId < 0)
			return;

		if (m_bEffectApplied && m_iAppliedCamera != cameraId)
			ClearPostProcess();

		m_iAppliedCamera = cameraId;
		m_iAppliedMode = m_iMode;

		if (m_iMode == 0)
		{
			m_World.SetCameraPostProcessEffect(cameraId, ACM_PP_THERMAL_SLOT, PostProcessEffectType.ThermalImaging, string.Empty);
			m_World.SetCameraPostProcessEffect(cameraId, ACM_PP_HDR_SLOT, PostProcessEffectType.HDR, m_sNightVisionMaterial);
		}
		else
		{
			m_World.SetCameraPostProcessEffect(cameraId, ACM_PP_HDR_SLOT, PostProcessEffectType.HDR, ACM_DEFAULT_HDR);
			m_World.SetCameraPostProcessEffect(cameraId, ACM_PP_THERMAL_SLOT, PostProcessEffectType.ThermalImaging, m_sThermalMaterial);
		}

		m_bEffectApplied = true;
	}

	//! Resets only the slots this component wrote, on the camera it wrote them to.
	protected void ClearPostProcess()
	{
		if (m_World && m_bEffectApplied && m_iAppliedCamera >= 0)
		{
			m_World.SetCameraPostProcessEffect(m_iAppliedCamera, ACM_PP_THERMAL_SLOT, PostProcessEffectType.ThermalImaging, string.Empty);
			m_World.SetCameraPostProcessEffect(m_iAppliedCamera, ACM_PP_HDR_SLOT, PostProcessEffectType.HDR, ACM_DEFAULT_HDR);
		}

		m_iAppliedCamera = -1;
		m_iAppliedMode = -1;
		m_bEffectApplied = false;
	}

	protected void ClearAppliedEffect()
	{
		ClearPostProcess();
		ACM_NVGTubeView.Release(this);
		m_bUsingTubeView = false;
	}

	override void EOnFrame(IEntity owner, float timeSlice)
	{
		EnsureInputListeners();
		if (!m_bNVGActive)
			return;

		if (!owner || !IsLocalWearer(owner))
		{
			m_bNVGActive = false;
			ClearAppliedEffect();
			return;
		}

		if (!m_World)
			return;

		// The tube camera index never changes; the view keeps itself aligned with the main camera.
		if (m_bUsingTubeView)
		{
			if (!ACM_NVGTubeView.IsActiveFor(this) || m_iMode != m_iAppliedMode)
				ApplyCurrentMode();
			return;
		}

		int cameraId = m_World.GetCurrentCameraId();
		if (cameraId < 0)
		{
			ClearPostProcess();
			return;
		}

		if (cameraId != m_iAppliedCamera || m_iMode != m_iAppliedMode)
			ApplyCurrentMode();
	}

	override void OnDelete(IEntity owner)
	{
		if (m_RegisteredInput)
		{
			m_RegisteredInput.RemoveActionListener(m_sPowerAction, EActionTrigger.DOWN, OnPowerAction);
			m_RegisteredInput.RemoveActionListener(m_sModeAction, EActionTrigger.DOWN, OnModeAction);
			m_RegisteredInput = null;
		}

		m_bNVGActive = false;
		ClearAppliedEffect();
		m_World = null;
		super.OnDelete(owner);
	}
}
