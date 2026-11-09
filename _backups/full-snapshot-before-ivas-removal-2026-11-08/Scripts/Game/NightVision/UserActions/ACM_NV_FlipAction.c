// ACM NVG Flip User Action
// Allows manual flip up/down of night vision goggles via interaction
//
// TWO PATHS, AND THE ORDER MATTERS.
//
// On a device with an ACM_TargetDetectionComponent that owns a visor, this action asks THAT component
// to flip and does not touch the signal itself. On a conventional NVG it writes the rotator directly,
// exactly as it always has.
//
// Writing the signal directly on an IVAS was a real defect, twice over. w_nvRotator is an MP signal,
// so it replicates - and this action declares HasLocalEffectOnlyScript, which means it runs on the
// machine that performed it and then quietly changes the visor for whoever actually owns the device.
// Worse, it left the detection component's own m_bVisorDown believing something untrue, so the next
// toggle wrote the value that was already there and nothing moved at all. A visor that has silently
// stopped responding to its own keybind is a hard thing to diagnose from inside the game.

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

		// Initial value 0, matching ACM_TargetDetectionComponent. Whichever of the two runs first
		// creates the signal and its initial value is the one that sticks, so the two calls have to
		// agree - and they must agree on STOWED. A rotator created deployed puts the visor across the
		// face of every character nothing is driving, which is what the entity and inventory previews
		// were showing.
		//
		// STOWED is 0 rather than 1 since the bind-pose re-rig: the models now rest stowed and
		// ACM_V2_NVG.siga multiplies by +145 to deploy. See ACM_NightVisionComponent.UpdateAnimation.
		if (m_pSignals)
			// Threshold must match ACM_TargetDetectionComponent. Whichever call creates the signal
			// sets its parameters, so a mismatch here would reintroduce the delayed-snap sync
			// depending only on which component happened to initialise first.
			m_iRotatorSignal = ACM_TargetDetectionComponent.EnsureAnimationSignal(pOwnerEntity, m_pSignals, ROTATOR_SIGNAL_NAME);
	}

	//------------------------------------------------------------------------------------------------
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		// Ask the owner, if there is one. It knows whether this machine is allowed to move the visor,
		// it keeps its own state in agreement with the signal, and it drives the scan and the feed off
		// the same flip - none of which happens if the signal is poked behind its back.
		ACM_TargetDetectionComponent detection = ACM_TargetDetectionComponent.Cast(pOwnerEntity.FindComponent(ACM_TargetDetectionComponent));
		if (detection && detection.HasVisorHardware())
		{
			detection.ToggleVisor();
			return;
		}

		// Conventional NVG. No visor owner, so this action is the owner.
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
