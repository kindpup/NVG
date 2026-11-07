//------------------------------------------------------------------------------------------------
// ACM_TacticalRoleInference.c
// Client-side fallback role derivation for players who have not declared one.
//
// THIS NEVER TOUCHES STORED STATE. The registry keeps UNASSIGNED for a player who has not chosen,
// and it stays UNASSIGNED forever unless that player chooses. Nothing here is replicated, nothing
// here is written back, and a player's declared role is never overwritten by what they happen to
// be carrying. Inference is a DISPLAY-TIME guess made independently on each client from whatever
// that client can already see, which is why it needs no network traffic at all: every client
// looking at the same friendly has the same entity in front of it and reaches the same answer.
//
// The design rule is conservatism. A wrong specialist marker is worse than no specialist marker -
// "that one is the medic" is a claim people act on - so every check here demands unambiguous
// evidence and anything short of that falls through to RIFLEMAN. Failing to identify a marksman
// costs nothing; labelling a rifleman as the medic could get someone killed waiting for aid that
// is not coming.
//
// Ordering, and why: vehicle context first, because a seat is unambiguous and describes what the
// person is doing RIGHT NOW; then deliberate specialist equipment; then the weapon in their hands,
// which is the weakest of the three because weapons get picked up, dropped and swapped. When a
// pilot dismounts, the vehicle evidence disappears and their weapon takes over - which is correct,
// not a bug.
//------------------------------------------------------------------------------------------------
class ACM_TacticalRoleInference
{
	//------------------------------------------------------------------------------------------------
	// Derive a role from what this entity visibly is and carries.
	//
	// Never returns UNASSIGNED. The whole point of the fallback is that every friendly ends up with
	// something drawable, and RIFLEMAN is the honest answer for "a person with a gun".
	//------------------------------------------------------------------------------------------------
	static ACM_ETacticalRole Infer(IEntity entity, array<string> medicalKeywords, array<string> radioKeywords)
	{
		if (!entity)
			return ACM_ETacticalRole.RIFLEMAN;

		// Null keyword lists mean "caller has no configuration to give us" - which happens when
		// inference runs without a registry component present at all. Falling back to the built-in
		// defaults keeps this usable standalone rather than silently disabling the equipment checks.
		if (!medicalKeywords)
			medicalKeywords = DefaultMedicalKeywords();

		if (!radioKeywords)
			radioKeywords = DefaultRadioKeywords();

		ACM_ETacticalRole fromVehicle = InferFromVehicle(entity);
		if (fromVehicle != ACM_ETacticalRole.UNASSIGNED)
			return fromVehicle;

		if (CarriesKeywordItem(entity, medicalKeywords))
			return ACM_ETacticalRole.MEDIC;

		ACM_ETacticalRole fromWeapon = InferFromWeapon(entity);
		if (fromWeapon != ACM_ETacticalRole.UNASSIGNED)
			return fromWeapon;

		if (CarriesKeywordItem(entity, radioKeywords))
			return ACM_ETacticalRole.RADIO_OPERATOR;

		return ACM_ETacticalRole.RIFLEMAN;
	}


	//------------------------------------------------------------------------------------------------
	// Built-in keyword defaults, matching the registry component's attribute defaults. They live
	// here as well so that inference works in a world whose game mode carries no registry - the
	// guess is entirely client-local and has never needed the network.
	//------------------------------------------------------------------------------------------------
	protected static ref array<string> s_aDefaultMedical;
	protected static ref array<string> s_aDefaultRadio;

	//------------------------------------------------------------------------------------------------
	protected static array<string> DefaultMedicalKeywords()
	{
		if (!s_aDefaultMedical)
			s_aDefaultMedical = { "Medikit", "MedicalKit", "FirstAidKit" };

		return s_aDefaultMedical;
	}

	//------------------------------------------------------------------------------------------------
	protected static array<string> DefaultRadioKeywords()
	{
		if (!s_aDefaultRadio)
			s_aDefaultRadio = { "Radio_Backpack", "RadioBackpack", "LongRangeRadio" };

		return s_aDefaultRadio;
	}

	//------------------------------------------------------------------------------------------------
	// Seat-based context.
	//
	// A pilot seat in something with a helicopter controller is a PILOT. A pilot seat in anything
	// else is a driver, which is CREW. A turret is CREW. Cargo is neither - a passenger being
	// driven somewhere is not aircrew, and treating every man in the back of a truck as CREW would
	// relabel an entire squad the moment they mounted up.
	//
	// UNASSIGNED here means "no vehicle evidence", not "no role" - the caller reads it as "keep
	// looking".
	//------------------------------------------------------------------------------------------------
	protected static ACM_ETacticalRole InferFromVehicle(IEntity entity)
	{
		CompartmentAccessComponent access = CompartmentAccessComponent.Cast(entity.FindComponent(CompartmentAccessComponent));
		if (!access || !access.IsInCompartment())
			return ACM_ETacticalRole.UNASSIGNED;

		BaseCompartmentSlot slot = access.GetCompartment();
		if (!slot)
			return ACM_ETacticalRole.UNASSIGNED;

		if (TurretCompartmentSlot.Cast(slot))
			return ACM_ETacticalRole.CREW;

		if (!PilotCompartmentSlot.Cast(slot))
			return ACM_ETacticalRole.UNASSIGNED;

		IEntity vehicle = slot.GetVehicle();
		if (vehicle && vehicle.FindComponent(HelicopterControllerComponent))
			return ACM_ETacticalRole.PILOT;

		return ACM_ETacticalRole.CREW;
	}

	//------------------------------------------------------------------------------------------------
	// Weapon in hand.
	//
	// Matched on the ENUM NAME rather than on hardcoded EWeaponType members, deliberately. The
	// member set is engine-side and differs between builds, so naming them directly is a compile
	// error waiting for a game update - and worse, a silent misclassification if the values are
	// ever renumbered. Reading the name through SCR_Enum means an unrecognised weapon type simply
	// matches nothing and falls through to RIFLEMAN, which is exactly the conservative failure this
	// module wants.
	//------------------------------------------------------------------------------------------------
	protected static ACM_ETacticalRole InferFromWeapon(IEntity entity)
	{
		BaseWeaponManagerComponent weapons = BaseWeaponManagerComponent.Cast(entity.FindComponent(BaseWeaponManagerComponent));
		if (!weapons)
			return ACM_ETacticalRole.UNASSIGNED;

		BaseWeaponComponent weapon = weapons.GetCurrentWeapon();
		if (!weapon)
			return ACM_ETacticalRole.UNASSIGNED;

		string typeName = SCR_Enum.GetEnumName(EWeaponType, weapon.GetWeaponType());
		if (typeName.IsEmpty())
			return ACM_ETacticalRole.UNASSIGNED;

		typeName.ToUpper();

		if (typeName.Contains("MACHINEGUN"))
			return ACM_ETacticalRole.AUTOMATIC_RIFLEMAN;

		if (typeName.Contains("GRENADELAUNCHER"))
			return ACM_ETacticalRole.GRENADIER;

		if (typeName.Contains("SNIPER"))
			return ACM_ETacticalRole.MARKSMAN;

		// Rocket launchers, pistols, rifles and anything unrecognised deliberately produce nothing.
		// There is no anti-tank role in the enum, and a rifle says only that its carrier is armed.
		return ACM_ETacticalRole.UNASSIGNED;
	}

	//------------------------------------------------------------------------------------------------
	// Inventory keyword match.
	//
	// Prefab-name substrings rather than a component test, because the components that would
	// identify a medical kit or a long-range radio are shared with items that mean nothing - every
	// soldier carries bandages, and a component check would mark the whole squad as medics. Item
	// NAMING, by contrast, is specific and is something a server owner can adjust for their own
	// faction mods without touching code.
	//
	// TWO HONEST LIMITATIONS, both of which fail safe:
	//
	//   1. A remote character's inventory contents are not reliably replicated to other clients.
	//      This check will often find nothing for other players and will then contribute nothing,
	//      leaving the weapon and vehicle evidence to decide. It does not guess to compensate.
	//   2. Keywords are authored, so an empty or mismatched list simply never fires.
	//
	// In both cases the result is a fall-through to RIFLEMAN rather than a wrong specialist marker,
	// which is the trade this module is built around.
	//------------------------------------------------------------------------------------------------
	protected static bool CarriesKeywordItem(IEntity entity, array<string> keywords)
	{
		if (!keywords || keywords.IsEmpty())
			return false;

		SCR_InventoryStorageManagerComponent inventory = SCR_InventoryStorageManagerComponent.Cast(entity.FindComponent(SCR_InventoryStorageManagerComponent));
		if (!inventory)
			return false;

		array<IEntity> items = {};
		inventory.GetItems(items);

		foreach (IEntity item : items)
		{
			if (!item)
				continue;

			EntityPrefabData prefabData = item.GetPrefabData();
			if (!prefabData)
				continue;

			ResourceName prefab = prefabData.GetPrefabName();
			if (prefab.IsEmpty())
				continue;

			string prefabText = prefab;
			prefabText.ToUpper();

			foreach (string keyword : keywords)
			{
				if (keyword.IsEmpty())
					continue;

				string needle = keyword;
				needle.ToUpper();

				if (prefabText.Contains(needle))
					return true;
			}
		}

		return false;
	}
}
