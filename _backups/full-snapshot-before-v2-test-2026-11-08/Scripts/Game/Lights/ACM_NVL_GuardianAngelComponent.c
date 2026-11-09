//------------------------------------------------------------------------------------------------
// ACM_NVL_GuardianAngelComponent.c
// FPS test patch: Guardian Angel device disabled.
//
// This file intentionally keeps the same public class/enums/API so existing prefab references,
// radial menu code, and network component references do not break. The component no longer
// spawns local lights, registers input listeners, starts callqueue flash loops, or performs RPC work.
//------------------------------------------------------------------------------------------------

enum ACM_GuardianAngelMode
{
	POLICE,
	FIRE,
	AMBULANCE,
	SECURITY,
	CUSTOM
}

enum ACM_GuardianAngelPattern
{
	ALTERNATE,
	DOUBLE_FLASH,
	TRIPLE_FLASH,
	WIG_WAG,
	STEADY_FLASH,
	QUAD_FLASH
}

[BaseContainerProps(), BaseContainerCustomTitleField("m_sLightName")]
class ACM_GuardianAngelLightConfig
{
	[Attribute("Light", UIWidgets.EditBox, desc: "Name for this light (disabled by FPS patch)")]
	string m_sLightName;

	[Attribute("", UIWidgets.ResourcePickerThumbnail, desc: "Light prefab to spawn (disabled by FPS patch)", params: "et")]
	ResourceName m_sLightPrefab;

	[Attribute("0 0 0", UIWidgets.Auto, desc: "Light position offset (disabled by FPS patch)")]
	ref PointInfo m_vOffset;

	[Attribute("0", UIWidgets.ComboBox, desc: "Light group (disabled by FPS patch)", enums: { ParamEnum("Group A (Primary)", "0"), ParamEnum("Group B (Secondary)", "1") })]
	int m_iGroup;
}

[ComponentEditorProps(category: "ACM/Lights", description: "Guardian Angel disabled by ACM FPS test patch")]
class ACM_NVL_GuardianAngelComponentClass : ScriptComponentClass {}

class ACM_NVL_GuardianAngelComponent : ScriptComponent
{
	[Attribute("", UIWidgets.Object, desc: "Disabled by FPS patch")]
	protected ref array<ref ACM_GuardianAngelLightConfig> m_aLightConfigs;

	[Attribute("", UIWidgets.ResourcePickerThumbnail, desc: "Disabled by FPS patch", params: "et")]
	protected ResourceName m_sLeftLightPrefab;

	[Attribute("", UIWidgets.ResourcePickerThumbnail, desc: "Disabled by FPS patch", params: "et")]
	protected ResourceName m_sRightLightPrefab;

	[Attribute("0 0 0", UIWidgets.Auto, desc: "Disabled by FPS patch")]
	protected ref PointInfo m_vLeftOffset;

	[Attribute("0 0 0", UIWidgets.Auto, desc: "Disabled by FPS patch")]
	protected ref PointInfo m_vRightOffset;

	[Attribute("0", UIWidgets.ComboBox, enums: ParamEnumArray.FromEnum(ACM_GuardianAngelMode), desc: "Disabled by FPS patch")]
	protected ACM_GuardianAngelMode m_eMode;

	[Attribute("0", UIWidgets.ComboBox, enums: ParamEnumArray.FromEnum(ACM_GuardianAngelPattern), desc: "Disabled by FPS patch")]
	protected ACM_GuardianAngelPattern m_ePattern;

	[Attribute("0.1", UIWidgets.Slider, desc: "Disabled by FPS patch", params: "0.01 1 0.01")]
	protected float m_fFlashDuration;

	[Attribute("0.05", UIWidgets.Slider, desc: "Disabled by FPS patch", params: "0.01 0.5 0.01")]
	protected float m_fFlashGap;

	[Attribute("8.0", UIWidgets.Slider, desc: "Disabled by FPS patch", params: "1 20 0.5")]
	protected float m_fBrightness;

	[Attribute("1 0 0", UIWidgets.ColorPicker, desc: "Disabled by FPS patch")]
	protected vector m_vCustomColor1;

	[Attribute("0 0 1", UIWidgets.ColorPicker, desc: "Disabled by FPS patch")]
	protected vector m_vCustomColor2;

	[Attribute("ACMToggleGuardianAngel", UIWidgets.EditBox, desc: "Disabled by FPS patch")]
	protected string m_sToggleAction;

	[Attribute("ACMGuardianAngelMode", UIWidgets.EditBox, desc: "Disabled by FPS patch")]
	protected string m_sModeAction;

	[Attribute("ACMGuardianAngelPattern", UIWidgets.EditBox, desc: "Disabled by FPS patch")]
	protected string m_sPatternAction;

	[Attribute("0", UIWidgets.CheckBox, desc: "Disabled by FPS patch")]
	protected bool m_bUseBuiltInReplication;

	protected bool m_bEnabled;
	protected bool m_bIsOnVehicle;
	protected IEntity m_Owner;

	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		m_Owner = owner;
		m_bEnabled = false;
	}

	override void EOnInit(IEntity owner)
	{
		m_Owner = owner;
		m_bEnabled = false;
	}

	bool IsEnabled() { return false; }
	ACM_GuardianAngelMode GetMode() { return m_eMode; }
	ACM_GuardianAngelPattern GetPattern() { return m_ePattern; }
	float GetBrightness() { return m_fBrightness; }
	bool IsOnVehicle() { return false; }
	int GetLightCount() { return 0; }
	int GetGroupACount() { return 0; }
	int GetGroupBCount() { return 0; }

	void SetEnabled(bool enabled) { m_bEnabled = false; }
	void SetMode(int mode) { m_eMode = mode; }
	void SetPattern(int pattern) { m_ePattern = pattern; }
	void SetBrightness(float brightness) { m_fBrightness = Math.Clamp(brightness, 1, 20); }
	void SetCustomColors(vector color1, vector color2) { m_vCustomColor1 = color1; m_vCustomColor2 = color2; }
	void SetFlashTiming(float duration, float gap) { m_fFlashDuration = Math.Clamp(duration, 0.01, 1.0); m_fFlashGap = Math.Clamp(gap, 0.01, 0.5); }
	void CycleMode() { int nextMode = m_eMode + 1; if (nextMode > 4) nextMode = 0; m_eMode = nextMode; }
	void CyclePattern() { int nextPattern = m_ePattern + 1; if (nextPattern > 5) nextPattern = 0; m_ePattern = nextPattern; }

	void SetEnabledFromNetwork(bool enabled) { m_bEnabled = false; }
	void SetModeFromNetwork(int mode) { m_eMode = mode; }
	void SetPatternFromNetwork(int pattern) { m_ePattern = pattern; }
	void SetBrightnessFromNetwork(float brightness) { m_fBrightness = Math.Clamp(brightness, 1, 20); }

	override void OnDelete(IEntity owner)
	{
		m_bEnabled = false;
		super.OnDelete(owner);
	}
}
