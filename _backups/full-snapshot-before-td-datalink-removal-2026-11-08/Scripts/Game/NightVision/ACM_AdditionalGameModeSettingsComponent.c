// ACM Game Mode Settings Extension
// Adds accessor for night noise effect configuration
// Compatible with NV System mod - works standalone or alongside it

modded class SCR_AdditionalGameModeSettingsComponent
{
	//------------------------------------------------------------------------------------------------
	// Safe accessor that works whether or not NV System is also loaded
	// Uses a unique name to avoid conflicts
	bool ACM_GetDisableNightNoise()
	{
		return m_bDisableNightNoiseEffect;
	}
}
