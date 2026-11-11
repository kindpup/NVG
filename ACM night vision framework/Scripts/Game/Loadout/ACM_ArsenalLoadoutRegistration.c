//
//  ACM
//  ACM Arsenal Loadout Registration
//
//  ACM Modular Framework implementation.
//==================================================================================================

class ACM_ArsenalLoadoutRegistration
{
	protected static bool s_bDidRegister;

	protected static void ACM_RegisterUniqueComponent(inout array<typename> components, typename componentType)
	{
		if (components.Find(componentType) == -1)
			components.Insert(componentType);
	}

	static void EnsureRegistered()
	{
		if (s_bDidRegister)
			return;

		if (!SCR_PlayerArsenalLoadout.ARSENALLOADOUT_COMPONENTS_TO_CHECK)
			return;

		ACM_RegisterUniqueComponent(SCR_PlayerArsenalLoadout.ARSENALLOADOUT_COMPONENTS_TO_CHECK, ACM_EquipmentStorageComponent);
		// Pocket is a legacy alias of ACM_EquipmentStorageComponent; keep it
		// registered for backward compatibility with existing prefab class usage.
		ACM_RegisterUniqueComponent(SCR_PlayerArsenalLoadout.ARSENALLOADOUT_COMPONENTS_TO_CHECK, ACM_PocketStorageComponent);
		ACM_RegisterUniqueComponent(SCR_PlayerArsenalLoadout.ARSENALLOADOUT_COMPONENTS_TO_CHECK, ACM_PouchStorageComponent);
		ACM_RegisterUniqueComponent(SCR_PlayerArsenalLoadout.ARSENALLOADOUT_COMPONENTS_TO_CHECK, ACM_UniversalStorageComponent);
		ACM_RegisterUniqueComponent(SCR_PlayerArsenalLoadout.ARSENALLOADOUT_COMPONENTS_TO_CHECK, ACM_ClothNodeStorageComponent);

		s_bDidRegister = true;
	}
}

