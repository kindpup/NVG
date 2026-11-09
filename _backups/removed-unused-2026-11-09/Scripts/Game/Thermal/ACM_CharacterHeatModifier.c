//------------------------------------------------------------------------------------------------
// ACM_CharacterHeatModifier.c
// Automatically registers characters as heat sources for thermal detection
//------------------------------------------------------------------------------------------------

modded class SCR_CharacterControllerComponent
{
	protected bool m_bACM_HeatRegistered = false;
	
	//------------------------------------------------------------------------------------------------
	override void OnControlledByPlayer(IEntity owner, bool controlled)
	{
		super.OnControlledByPlayer(owner, controlled);
		
		// Register all characters as heat sources
		if (!m_bACM_HeatRegistered)
		{
			EnsureACMHeatSource(owner);
		}
	}
	
	//------------------------------------------------------------------------------------------------
	override protected void OnInit(IEntity owner)
	{
		super.OnInit(owner);
		
		// Delay registration to ensure thermal system exists
		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().CallLater(DelayedACMHeatRegistration, 500, false, owner);
	}
	
	//------------------------------------------------------------------------------------------------
	protected void DelayedACMHeatRegistration(IEntity owner)
	{
		if (!owner || m_bACM_HeatRegistered)
			return;
		
		EnsureACMHeatSource(owner);
	}
	
	//------------------------------------------------------------------------------------------------
	protected void EnsureACMHeatSource(IEntity owner)
	{
		if (!owner || m_bACM_HeatRegistered)
			return;

		// Client-only. This is a modded SCR_CharacterControllerComponent, so without this
		// guard it runs for every character on the dedicated server too - registering heat
		// sources nothing will ever render and printing a line per character while it does
		// it. Thermal is a local visual; the server has no business tracking it.
		//
		// GetWorkspace() is null on a dedicated server, which is the same test the target
		// detection system uses to stay off the server.
		if (!GetGame() || !GetGame().GetWorkspace())
		{
			m_bACM_HeatRegistered = true;   // never reconsider on this machine
			return;
		}
		
		// Check if entity already has a heat source component
		ACM_HeatSourceComponent existingHeat = ACM_HeatSourceComponent.Cast(owner.FindComponent(ACM_HeatSourceComponent));
		if (existingHeat)
		{
			m_bACM_HeatRegistered = true;
			return;
		}
		
		// Register character as virtual heat source with thermal system
		ACM_ThermalSystem system = ACM_ThermalSystem.GetInstance();
		if (system)
		{
			// Characters have base heat 0.85, detection range 150m
			system.RegisterCharacterHeatSource(owner, 0.85, 150.0);
			m_bACM_HeatRegistered = true;

			// No log here on purpose: this fires once per character, and on a populated
			// server that is hundreds of lines of nothing.
		}
		else
		{
			Print("[ACM_CharacterHeat] Failed to get ThermalSystem instance", LogLevel.WARNING);
			// Retry later
			if (GetGame() && GetGame().GetCallqueue())
				GetGame().GetCallqueue().CallLater(DelayedACMHeatRegistration, 1000, false, owner);
		}
	}
}
