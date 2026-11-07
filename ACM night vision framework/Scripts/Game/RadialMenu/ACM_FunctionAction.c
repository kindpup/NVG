//------------------------------------------------------------------------------------------------
// ACM_FunctionAction.c
// Base class for radial menu actions
// Part of ACM Night Vision and Lights mod
//------------------------------------------------------------------------------------------------

class ACM_FunctionAction : ScriptedUserAction 
{
	[Attribute("{3262679C50EF4F01}UI/Textures/Icons/icons_wrapperUI.imageset", UIWidgets.ResourceNamePicker, desc: "Image set for action icon")]
	protected ResourceName m_sImageSet;
	
	[Attribute("light", UIWidgets.EditBox, desc: "Icon name from imageset")]
	protected string m_sActionIcon;
	
	ResourceName GetImageSet()
	{
		return m_sImageSet;
	}
	
	string GetActionIcon()
	{
		return m_sActionIcon;
	}
}
