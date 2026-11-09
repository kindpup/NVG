//------------------------------------------------------------------------------------------------
// ACM_AnimationComponent.c
// Item animation component for ACM night vision devices.
//
// This is the ACM equivalent of NVsystem's NVS_AnimationComponent, restored after it went missing
// from the ACM lineage. ACM was originally derived from NVS, which drove the goggle flip through
// TWO paths in priority order:
//
//   1. An animation-graph COMMAND - CMD_FLIP_ACTION - on a BaseItemAnimationComponent. This is the
//      real path for a device with an authored .agr/.asi and flip clips.
//   2. A procedural animation SIGNAL - w_nvRotator - as the fallback for a device that has only a
//      ProcAnimComponent rotating a bone.
//
// ACM kept only the second and dropped the first, so any device authored for the command path had
// nothing driving it. Restoring this component restores that path; it does not replace the signal
// fallback, which stays exactly where it was for devices that use it.
//
// Modelled on NVS_AnimationComponent deliberately, including binding the command in the CONSTRUCTOR
// rather than in an init event. BindCommand has to happen while the component is being built, and
// the edit-mode guard exists because Workbench instantiates components outside a running animation
// system where the bind is meaningless.
//------------------------------------------------------------------------------------------------
class ACM_AnimationComponentClass : BaseItemAnimationComponentClass
{
}

class ACM_AnimationComponent : BaseItemAnimationComponent
{
	// Resolved once at construction. Negative means the command is not available on this device's
	// animation graph, in which case this component quietly does nothing and the caller falls
	// through to the signal path.
	protected int m_iFlipCmdId = -1;

	//------------------------------------------------------------------------------------------------
	void ACM_AnimationComponent(IEntityComponentSource src, IEntity ent, IEntity parent)
	{
		if (SCR_Global.IsEditMode())
			return;

		m_iFlipCmdId = BindCommand("CMD_FLIP_ACTION");
	}

	//------------------------------------------------------------------------------------------------
	//! Is the flip command actually bound? Lets a caller log the difference between "no component"
	//! and "component present but its graph has no CMD_FLIP_ACTION".
	bool HasFlipCommand()
	{
		return m_iFlipCmdId >= 0;
	}

	//------------------------------------------------------------------------------------------------
	//! Drive the flip. `active` is the device's activation state - true lowers the goggles into
	//! view, false raises them. `skip` jumps straight to the end pose instead of playing the
	//! transition, which is what a state restore on stream-in wants.
	void FlipNVAction(bool active, bool skip)
	{
		if (m_iFlipCmdId < 0)
			return;

		CallCommand(m_iFlipCmdId, active, skip);
	}
}
