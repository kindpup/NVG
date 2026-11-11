//------------------------------------------------------------------------------------------------
// ACM_ThermalPalette.c
// Thermal palette definition for color modes
//------------------------------------------------------------------------------------------------

[BaseContainerProps(configRoot: true)]
class ACM_ThermalPalette
{
	[Attribute("White Hot", UIWidgets.EditBox, desc: "Display name for this palette")]
	string m_sName;
	
	[Attribute("", UIWidgets.ResourceNamePicker, desc: "HDR material for this thermal palette", "emat")]
	ResourceName m_Material;
	
	[Attribute("1.5", UIWidgets.Slider, desc: "Default brightness for this palette", params: "0.1 5 0.1")]
	float m_fDefaultBrightness;
	
	[Attribute("1.2", UIWidgets.Slider, desc: "Default contrast for this palette", params: "0.5 3 0.1")]
	float m_fDefaultContrast;
	
	[Attribute("", UIWidgets.ResourceNamePicker, desc: "ECOTI only: version of this palette with the outline baked in, used in Fused and Thermal modes so the outline stays visible. Must be a ThermalImaging material under UI/Materials/Editor/Thermal *. Empty = plain palette.", "emat")]
	ResourceName m_OutlinedMaterial;

	string GetName() { return m_sName; }
	ResourceName GetMaterial() { return m_Material; }
	ResourceName GetOutlinedMaterial() { return m_OutlinedMaterial; }
	float GetDefaultBrightness() { return m_fDefaultBrightness; }
	float GetDefaultContrast() { return m_fDefaultContrast; }
}
