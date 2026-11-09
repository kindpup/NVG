// ------------------------------------------------------------------------------
// ACM_NVL_LightSource
// Light source definition - shared between flashlights and IR lights
// Part of ACM Night Vision and Lights mod
// ------------------------------------------------------------------------------

enum ACM_NVL_LightType
{
	FLASHLIGHT,
	IRLIGHT
}

[BaseContainerProps(configRoot: true)]
class ACM_NVL_LightSource : ScriptAndConfig
{
	[Attribute("", UIWidgets.ResourceNamePicker, desc: "Light prefab (LightEntity prefab only!)", "et")]
	protected ResourceName m_sLightPrefab;

	[Attribute("0 0 0", UIWidgets.Auto, desc: "Local offset for the light")]
	protected ref PointInfo m_vOffset;

	[Attribute("1 1 1", UIWidgets.ColorPicker, desc: "Emissive glow color on the helmet")]
	protected vector m_vLensColor;

	[Attribute("10", "0 500 1", desc: "Emissive brightness on the helmet")]
	protected float m_fLensIntensity;

	[Attribute("0", UIWidgets.ComboBox, enums: ParamEnumArray.FromEnum(ACM_NVL_LightType), desc: "Light type")]
	protected ACM_NVL_LightType m_eType;

	[Attribute("", desc: "Toggle sound event (optional)")]
	protected string m_sSoundEvent;
	
	// Per-source brightness settings
	[Attribute("6.0", UIWidgets.Slider, desc: "Default brightness for this light (LV)", params: "0.1 20 0.1", category: "Brightness")]
	protected float m_fDefaultBrightness;
	
	[Attribute("0.1", UIWidgets.Slider, desc: "Minimum brightness for this light (LV)", params: "0.01 5 0.01", category: "Brightness")]
	protected float m_fMinBrightness;
	
	[Attribute("6.0", UIWidgets.Slider, desc: "Maximum brightness for this light (LV)", params: "1 25 0.5", category: "Brightness")]
	protected float m_fMaxBrightness;
	
	[Attribute("0.2", UIWidgets.Slider, desc: "Brightness adjustment step for this light", params: "0.01 1 0.01", category: "Brightness")]
	protected float m_fBrightnessStep;

	// Per-source beam cone angle settings (for spotlight LightEntity sources)
	[Attribute("1", UIWidgets.CheckBox, desc: "Enable beam cone angle (spotlight width) adjustment for this source. Only affects LightEntity-based lights.", category: "Beam")]
	protected bool m_bSupportsConeControl;

	[Attribute("18", UIWidgets.Slider, desc: "Default beam cone angle in degrees", params: "1 90 1", category: "Beam")]
	protected int m_iDefaultConeAngle;

	[Attribute("4", UIWidgets.Slider, desc: "Minimum beam cone angle in degrees", params: "1 45 1", category: "Beam")]
	protected int m_iMinConeAngle;

	[Attribute("55", UIWidgets.Slider, desc: "Maximum beam cone angle in degrees", params: "5 90 1", category: "Beam")]
	protected int m_iMaxConeAngle;

	[Attribute("5", UIWidgets.Slider, desc: "Beam cone angle adjustment step in degrees", params: "1 20 1", category: "Beam")]
	protected int m_iConeAngleStep;

	// Accessors
	ResourceName GetPrefab() { return m_sLightPrefab; }
	PointInfo GetOffset() { return m_vOffset; }
	vector GetLensColor() { return m_vLensColor; }
	float GetLensIntensity() { return m_fLensIntensity; }

	bool IsFlashlight() { return m_eType == ACM_NVL_LightType.FLASHLIGHT; }
	bool IsIR() { return m_eType == ACM_NVL_LightType.IRLIGHT; }

	string GetSoundEvent() { return m_sSoundEvent; }
	
	// Brightness accessors
	float GetDefaultBrightness() { return m_fDefaultBrightness; }
	float GetMinBrightness() { return m_fMinBrightness; }
	float GetMaxBrightness() { return m_fMaxBrightness; }
	float GetBrightnessStep() { return m_fBrightnessStep; }

	// Cone angle accessors
	bool SupportsConeControl() { return m_bSupportsConeControl; }
	int GetDefaultConeAngle() { return m_iDefaultConeAngle; }
	int GetMinConeAngle() { return m_iMinConeAngle; }
	int GetMaxConeAngle() { return m_iMaxConeAngle; }
	int GetConeAngleStep() { return m_iConeAngleStep; }
}
