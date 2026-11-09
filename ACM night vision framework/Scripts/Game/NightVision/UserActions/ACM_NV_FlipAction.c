// ACM NVG Flip User Action
// Allows manual flip up/down of night vision goggles via interaction: writes the rotator directly.

class ACM_NV_FlipActionUserAction : ScriptedUserAction
{
	protected SignalsManagerComponent m_pSignals;
	protected int m_iRotatorSignal = -1;

	protected static const string ROTATOR_SIGNAL_NAME = "w_nvRotator";

	//------------------------------------------------------------------------------------------------
	override bool HasLocalEffectOnlyScript()
	{
		return true;
	}

	//------------------------------------------------------------------------------------------------
	override void Init(IEntity pOwnerEntity, GenericComponent pManagerComponent)
	{
		super.Init(pOwnerEntity, pManagerComponent);

		m_pSignals = SignalsManagerComponent.Cast(pOwnerEntity.FindComponent(SignalsManagerComponent));

		// Created through the shared helper so the parameters (initial 0 = STOWED since the bind-pose
		// re-rig) match whichever creator runs first. See ACM_NightVisionComponent.UpdateAnimation.
		if (m_pSignals)
			m_iRotatorSignal = ACM_NightVisionComponent.EnsureAnimationSignal(pOwnerEntity, m_pSignals, ROTATOR_SIGNAL_NAME);
	}

	//------------------------------------------------------------------------------------------------
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		if (m_iRotatorSignal < 0 || !m_pSignals)
			return;

		// Toggle between up and down positions
		float currentPos = m_pSignals.GetSignalValue(m_iRotatorSignal);
		float newPos = 1.0;
		if (currentPos >= 0.5)
			newPos = 0.0;

		m_pSignals.SetSignalValue(m_iRotatorSignal, newPos);
	}
}
