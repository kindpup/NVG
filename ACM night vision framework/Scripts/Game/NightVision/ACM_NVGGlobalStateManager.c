//------------------------------------------------------------------------------------------------
// ACM_NVGGlobalStateManager.c
// Global state manager for NVG vanilla grain effect
// Tracks active NVG count across all players to prevent global state conflicts
//------------------------------------------------------------------------------------------------

class ACM_NVGGlobalStateManager
{
	protected static ref ACM_NVGGlobalStateManager s_Instance;
	protected int m_iActiveNVGCount = 0;
	protected bool m_bVanillaGrainDisabled = false;
	
	static ACM_NVGGlobalStateManager GetInstance()
	{
		if (!s_Instance)
			s_Instance = new ACM_NVGGlobalStateManager();
		return s_Instance;
	}
	
	void ACM_NVGGlobalStateManager()
	{
		m_iActiveNVGCount = 0;
		m_bVanillaGrainDisabled = false;
	}
	
	//------------------------------------------------------------------------------------------------
	// Register that an NVG is now active (call when NVG turns on)
	//------------------------------------------------------------------------------------------------
	static void RegisterActiveNVG()
	{
		ACM_NVGGlobalStateManager inst = GetInstance();
		if (!inst)
			return;
		
		inst.m_iActiveNVGCount++;
		
		// Only disable vanilla grain if this is the first active NVG
		if (inst.m_iActiveNVGCount == 1 && !inst.m_bVanillaGrainDisabled)
		{
			inst.DisableVanillaGrain(true);
		}
	}
	
	//------------------------------------------------------------------------------------------------
	// Unregister that an NVG is no longer active (call when NVG turns off)
	//------------------------------------------------------------------------------------------------
	static void UnregisterActiveNVG()
	{
		ACM_NVGGlobalStateManager inst = GetInstance();
		if (!inst)
			return;
		
		if (inst.m_iActiveNVGCount > 0)
			inst.m_iActiveNVGCount--;
		
		// Only re-enable vanilla grain if no NVGs are active
		if (inst.m_iActiveNVGCount == 0 && inst.m_bVanillaGrainDisabled)
		{
			inst.DisableVanillaGrain(false);
		}
	}
	
	//------------------------------------------------------------------------------------------------
	// Reset count (call on disconnect or world reset)
	//------------------------------------------------------------------------------------------------
	static void Reset()
	{
		ACM_NVGGlobalStateManager inst = GetInstance();
		if (!inst)
			return;
		
		if (inst.m_bVanillaGrainDisabled)
		{
			inst.DisableVanillaGrain(false);
		}
		
		inst.m_iActiveNVGCount = 0;
	}
	
	//------------------------------------------------------------------------------------------------
	// Internal method to actually disable/enable vanilla grain
	//------------------------------------------------------------------------------------------------
	protected void DisableVanillaGrain(bool disable)
	{
		if (RplSession.Mode() == RplMode.None)
			return;
		
		SCR_AdditionalGameModeSettingsComponent gmSettings = SCR_AdditionalGameModeSettingsComponent.GetInstance();
		if (gmSettings && gmSettings.ACM_GetDisableNightNoise())
			return;
		
		m_bVanillaGrainDisabled = disable;
		SCR_NoiseFilterEffect.SetNightNoiseEffectState(disable);
	}
	
}
