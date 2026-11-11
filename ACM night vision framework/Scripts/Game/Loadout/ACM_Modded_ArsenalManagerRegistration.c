//==================================================================================================
//  ACM
//  ACM Arsenal Registration Bootstrap
//==================================================================================================

// Ensure ACM loadout storage components are registered before any system (including
// third-party editors such as Bacon BLE) saves or applies loadout strings.
modded class SCR_ArsenalManagerComponent
{
	override void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);
		ACM_ArsenalLoadoutRegistration.EnsureRegistered();
	}
}

