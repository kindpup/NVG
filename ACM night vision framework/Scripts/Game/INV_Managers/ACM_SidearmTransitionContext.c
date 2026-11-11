//
//  ACM
//  ACM Sidearm Transition Context
//
//  Original ACM-owned script for the modular framework.
//==================================================================================================

// Immutable snapshot of one Reforger weapon transition. The router uses it to separate raw engine events from ACM weapon-stow decisions.
class ACM_SidearmTransitionContext
{
	AnimationEventID TransitionEvent;
	IEntity PreviousWeapon;
	IEntity CurrentWeapon;
	EWeaponType CurrentWeaponType;
	bool PreviousWasSidearm;
	bool CurrentIsSidearm;
	bool PreviousWasRifle;
	bool CurrentIsRifle;

	// Derives routed-weapon flags once so later routing decisions do not need to re-query live weapon state.
	void ACM_SidearmTransitionContext(
		AnimationEventID transitionEvent,
		IEntity previousWeapon,
		IEntity currentWeapon,
		EWeaponType currentWeaponType)
	{
		TransitionEvent = transitionEvent;
		PreviousWeapon = previousWeapon;
		CurrentWeapon = currentWeapon;
		CurrentWeaponType = currentWeaponType;
		PreviousWasSidearm = ACM_ItemClassificationService.IsHolsterSidearm(previousWeapon);
		CurrentIsSidearm = ACM_ItemClassificationService.IsHolsterSidearm(currentWeapon);
		PreviousWasRifle = ACM_ItemClassificationService.IsRifle(previousWeapon);
		CurrentIsRifle = ACM_ItemClassificationService.IsRifle(currentWeapon);
	}
}
