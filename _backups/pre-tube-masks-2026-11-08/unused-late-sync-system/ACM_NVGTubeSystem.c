//------------------------------------------------------------------------------------------------
// Re-syncs the NVG tube camera at WorldSystemPoint.PostFrame, which runs after EVERY entity's
// EOnPostFrame. The player camera copies itself to the world camera in its own EOnPostFrame, and
// entity event order is not fixed, so syncing from an entity event sometimes copied last frame's
// view - visible as jitter in the tubes while aiming. Registered in Configs/Systems/ChimeraSystemsConfig.conf.
//------------------------------------------------------------------------------------------------
class ACM_NVGTubeSystem : GameSystem
{
	//------------------------------------------------------------------------------------------------
	override static void InitInfo(WorldSystemInfo outInfo)
	{
		outInfo
			.SetAbstract(false)
			.SetLocation(WorldSystemLocation.Both)
			.AddPoint(WorldSystemPoint.PostFrame);
	}

	//------------------------------------------------------------------------------------------------
	protected bool m_bLogged;

	//------------------------------------------------------------------------------------------------
	protected override void OnUpdatePoint(WorldUpdatePointArgs args)
	{
		if (!m_bLogged)
		{
			m_bLogged = true;
			Print("[ACM NVG] Tube camera late-sync system running.", LogLevel.NORMAL);
		}

		ACM_NVGTubeView.LateSync();
	}
}
