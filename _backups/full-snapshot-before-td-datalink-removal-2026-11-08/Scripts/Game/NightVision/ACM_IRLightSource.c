// ACM Infrared Light Source Component
// Base class for objects that emit IR-visible light

class ACM_IRLightSourceClass : ScriptComponentClass {}

class ACM_IRLightSource : ScriptComponent
{
	protected bool m_bVisibleUnderIR = false;
	
	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		SetEventMask(owner, EntityEvent.INIT);
	}
	
	//------------------------------------------------------------------------------------------------
	override void EOnInit(IEntity owner)
	{
		if (SCR_Global.IsEditMode())
			return;

		if (Replication.IsRunning() && Replication.IsServer() && !GetGame().GetPlayerController())
			return;
		
		ACM_IRSystem sys = ACM_IRSystem.GetInstance();
		if (sys)
			sys.AddSource(this);
	}
	
	//------------------------------------------------------------------------------------------------
	void ToggleIRVisibility(bool show)
	{
		m_bVisibleUnderIR = show;
		// Subclasses implement specific visibility behavior
	}
	
	//------------------------------------------------------------------------------------------------
	bool IsVisibleUnderIR()
	{
		return m_bVisibleUnderIR;
	}
	
	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		ACM_IRSystem sys = ACM_IRSystem.GetInstance();
		if (sys)
			sys.RemoveSource(this);
		
		super.OnDelete(owner);
	}
}
