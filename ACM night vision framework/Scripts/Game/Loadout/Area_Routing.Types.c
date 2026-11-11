// =============================================================================
//                            ACM FRAMEWORK
// -----------------------------------------------------------------------------
// ACM Area Routing Type Registry
//
// Authoritative LoadoutAreaType -> ACM_EAreaFamily mapping for cloth-area
// routing. Add an entry to `ACM_AREA_TYPE_ROUTING_MAPPINGS` to wire a new area
// type into the area-family system without editing any service code.
//
// What this enables:
//   - One-shot family lookup: "given any LoadoutAreaType, what semantic family
//     is it?" (e.g. `ACM_Waist` -> `WAIST`, `LoadoutHeadCoverArea` ->
//     `HEAD_COVER`).
//   - Cross-area equivalence: a cloth that lives at canonical area `A` and a
//     cloth at canonical area `B` can be treated uniformly when both areas
//     belong to the same family.
//
// What this does NOT enable:
//   - Engine-level equip acceptance. The engine still matches a worn cloth to
//     its single canonical `AreaType`. Use `ACM_ClothAreaExtensionComponent`
//     on the cloth prefab to declare additional areas a cloth satisfies for
//     ACM-side queries.
//
// How to wire a new area type:
//   1. Declare the area class in `Loadout/ACM_LoadoutAreas.c` (keep A-Z order
//      inside its section).
//   2. If the family doesn't already exist, APPEND a value to
//      `enum ACM_EAreaFamily` below. Never insert in the middle — the values
//      are positional.
//   3. Append `new ACM_AreaRoutingMapping(ACM_MyArea, ACM_EAreaFamily.MY_FAMILY)`
//      to `ACM_AREA_TYPE_ROUTING_MAPPINGS`.
//
// Notes:
//   - Position-unique families (e.g. EAR_LEFT vs EAR_RIGHT) require a 1:1
//     area-to-family pairing so a cloth cannot drift across positions.
//   - Every framework-owned area below is currently mapped 1:1 to its own
//     family. That makes `IsRoutableArea` true for all of them (they no longer
//     resolve to NONE) while keeping `AreasShareFamily` behaviour identical to
//     before — nothing became interchangeable that wasn't already. The flip
//     side is that family equivalence buys nothing until two areas are
//     deliberately pointed at the SAME family value. Do that (and test it)
//     when you actually want two areas treated as one.
//   - `NVG_MOUNT`, `NIGHTVISION`, `EAR_LEFT`, `EAR_RIGHT` and the `HELMET_*`
//     values are RESERVED placeholders with no mappings. The areas they
//     describe live in add-on mods (ACM Helmets, ACM NV & Lights), not here.
//   - Only seed framework-internal area types here. An add-on mod that wants
//     its areas routed must `ACM_AreaRoutingTypes.ACM_AREA_TYPE_ROUTING_MAPPINGS
//     .Insert(new ACM_AreaRoutingMapping(...))` from its own init path — the
//     array is a public static with a literal initializer, there is no
//     registration hook. Nothing does this today, which is why every add-on
//     area resolves to NONE.
// =============================================================================

enum ACM_EAreaFamily
{
	NONE = 0,
	HEAD_COVER,
	NVG_MOUNT,
	NIGHTVISION,
	EAR_LEFT,
	EAR_RIGHT,
	HELMET_RAIL,
	HELMET_TOP,
	HELMET_FRONT,
	HELMET_LIGHT,
	WAIST,
	LEG_LEFT,
	LEG_RIGHT,
	SLING,
	MASK,
	BACKPANEL,
	PLACARD,
	BELT,
	CARABINER,

	// --- Appended: core / shared -------------------------------------------
	ACCESSORY_GENERIC,
	CARABINER_LARGE,
	CARABINER_SMALL,
	CHEMLIGHT,
	STORAGE_POUCH_SMALL,

	// --- Appended: torso apparel & patches ---------------------------------
	GHILLIE,
	PATCH_FLAG,
	PATCH_SHIRT,

	// --- Appended: vest panels & pouches -----------------------------------
	PLACARD_POUCHES,
	VEST_SINGLE_POUCHES,
	ADMIN_POUCH_FRONT,
	SHOULDER_ACCESSORY,
	VEST_BELLY_POUCH,
	VEST_CHEST_ACCESSORY,
	VEST_HOLSTER,

	// --- Appended: armor plates --------------------------------------------
	PLATE_FRONT,
	PLATE_BACK,
	PLATE_LEFT,
	PLATE_RIGHT,

	// --- Appended: cosmetic ------------------------------------------------
	VEST_COLOR,

	// --- Appended: comms ---------------------------------------------------
	MIC_CHORD,
	MIC_MPU5,
	RADIO_ANTENNA,
	RADIO_POUCH,

	// --- Appended: belt ----------------------------------------------------
	BELT_DUMP_POUCH,
	BELT_FANNY_PACK,
	BELT_HOLSTER,
	BELT_HOLSTER_STRAP,
	BELT_KNIFE,
	BELT_MAG_POUCH,
	BELT_NADE,
	BELT_PISTOL_MAG_POUCH,
	BELT_REAR_POUCH,
	BELT_TETHER,
	BELT_DOUBLE_AR_POUCH,

	// --- Appended: legs ----------------------------------------------------
	LEG_POUCH,
	TOURNIQUET,

	// --- Appended: backpack / bag ------------------------------------------
	BAG_POUCH_SETUP,
	BAG_SLOT_BOTTOM,
	BAG_SLOT_LEFT,
	BAG_SLOT_RIGHT
}

class ACM_AreaRoutingMapping
{
	typename m_AreaType;
	ACM_EAreaFamily m_Family;

	void ACM_AreaRoutingMapping(typename areaType, ACM_EAreaFamily family)
	{
		m_AreaType = areaType;
		m_Family = family;
	}
}

class ACM_AreaRoutingTypes
{
	// Authoring list. Append entries to wire new area types into the family
	// system. Each entry binds one LoadoutAreaType subclass to a ACM_EAreaFamily.
	// Order mirrors the section order in `Loadout/ACM_LoadoutAreas.c`.
	static ref array<ref ACM_AreaRoutingMapping> ACM_AREA_TYPE_ROUTING_MAPPINGS = {
		// --- Vanilla anchors ---------------------------------------------------
		new ACM_AreaRoutingMapping(LoadoutHeadCoverArea, ACM_EAreaFamily.HEAD_COVER),

		// --- Core / shared -----------------------------------------------------
		new ACM_AreaRoutingMapping(ACM_Accessory, ACM_EAreaFamily.ACCESSORY_GENERIC),
		new ACM_AreaRoutingMapping(ACM_Carabiner, ACM_EAreaFamily.CARABINER),
		new ACM_AreaRoutingMapping(ACM_Chemlights, ACM_EAreaFamily.CHEMLIGHT),
		new ACM_AreaRoutingMapping(ACM_Large_Carabiner, ACM_EAreaFamily.CARABINER_LARGE),
		new ACM_AreaRoutingMapping(ACM_Small_Carabiner, ACM_EAreaFamily.CARABINER_SMALL),
		new ACM_AreaRoutingMapping(ACM_Small_Storage_Pouch, ACM_EAreaFamily.STORAGE_POUCH_SMALL),

		// --- Head & face -------------------------------------------------------
		new ACM_AreaRoutingMapping(ACM_Mask, ACM_EAreaFamily.MASK),

		// --- Torso apparel & patches -------------------------------------------
		new ACM_AreaRoutingMapping(ACM_FlagPatch, ACM_EAreaFamily.PATCH_FLAG),
		new ACM_AreaRoutingMapping(ACM_Ghillie, ACM_EAreaFamily.GHILLIE),
		new ACM_AreaRoutingMapping(ACM_ShirtPatch, ACM_EAreaFamily.PATCH_SHIRT),

		// --- Vest panels & placards --------------------------------------------
		new ACM_AreaRoutingMapping(ACM_Vest_Backpanel, ACM_EAreaFamily.BACKPANEL),
		new ACM_AreaRoutingMapping(ACM_Vest_Mag_Placard, ACM_EAreaFamily.PLACARD),
		new ACM_AreaRoutingMapping(ACM_Vest_Placard_Pouches, ACM_EAreaFamily.PLACARD_POUCHES),
		new ACM_AreaRoutingMapping(ACM_Vest_Single_Pouches, ACM_EAreaFamily.VEST_SINGLE_POUCHES),

		// --- Vest pouches & accessories ----------------------------------------
		new ACM_AreaRoutingMapping(ACM_Front_Admin_Pouch, ACM_EAreaFamily.ADMIN_POUCH_FRONT),
		new ACM_AreaRoutingMapping(ACM_Shoulder_Accessory, ACM_EAreaFamily.SHOULDER_ACCESSORY),
		new ACM_AreaRoutingMapping(ACM_Vest_Belly_Pouch, ACM_EAreaFamily.VEST_BELLY_POUCH),
		new ACM_AreaRoutingMapping(ACM_Vest_Chest_Accessories, ACM_EAreaFamily.VEST_CHEST_ACCESSORY),
		new ACM_AreaRoutingMapping(ACM_Vest_Holster, ACM_EAreaFamily.VEST_HOLSTER),

		// --- Armor plates ------------------------------------------------------
		new ACM_AreaRoutingMapping(ACM_Back_Plate, ACM_EAreaFamily.PLATE_BACK),
		new ACM_AreaRoutingMapping(ACM_Front_Plate, ACM_EAreaFamily.PLATE_FRONT),
		new ACM_AreaRoutingMapping(ACM_Left_Plate, ACM_EAreaFamily.PLATE_LEFT),
		new ACM_AreaRoutingMapping(ACM_Right_Plate, ACM_EAreaFamily.PLATE_RIGHT),

		// --- Cosmetic ----------------------------------------------------------
		new ACM_AreaRoutingMapping(ACM_Vest_JPC_Color, ACM_EAreaFamily.VEST_COLOR),

		// --- Comms & radio -----------------------------------------------------
		new ACM_AreaRoutingMapping(ACM_Long_Mic_Chord, ACM_EAreaFamily.MIC_CHORD),
		new ACM_AreaRoutingMapping(ACM_MPU5_Microphone, ACM_EAreaFamily.MIC_MPU5),
		new ACM_AreaRoutingMapping(ACM_Radio_antena, ACM_EAreaFamily.RADIO_ANTENNA),
		new ACM_AreaRoutingMapping(ACM_Radio_Pouch, ACM_EAreaFamily.RADIO_POUCH),

		// --- Belt & waist ------------------------------------------------------
		new ACM_AreaRoutingMapping(ACM_Belt_Dump_Pouch, ACM_EAreaFamily.BELT_DUMP_POUCH),
		new ACM_AreaRoutingMapping(ACM_Belt_Fanny_Pack, ACM_EAreaFamily.BELT_FANNY_PACK),
		new ACM_AreaRoutingMapping(ACM_Belt_Holster, ACM_EAreaFamily.BELT_HOLSTER),
		new ACM_AreaRoutingMapping(ACM_Belt_Holster_Strap, ACM_EAreaFamily.BELT_HOLSTER_STRAP),
		new ACM_AreaRoutingMapping(ACM_Belt_Knife_Slot, ACM_EAreaFamily.BELT_KNIFE),
		new ACM_AreaRoutingMapping(ACM_Belt_Mag_Pouch, ACM_EAreaFamily.BELT_MAG_POUCH),
		new ACM_AreaRoutingMapping(ACM_Belt_Nade_Slot, ACM_EAreaFamily.BELT_NADE),
		new ACM_AreaRoutingMapping(ACM_Belt_Pistol_Mag_Pouch, ACM_EAreaFamily.BELT_PISTOL_MAG_POUCH),
		new ACM_AreaRoutingMapping(ACM_Belt_Rear_Pouch, ACM_EAreaFamily.BELT_REAR_POUCH),
		new ACM_AreaRoutingMapping(ACM_Belt_Tether_Slot, ACM_EAreaFamily.BELT_TETHER),
		new ACM_AreaRoutingMapping(ACM_BeltGunClip, ACM_EAreaFamily.BELT),
		new ACM_AreaRoutingMapping(ACM_Double_AR_Pouch, ACM_EAreaFamily.BELT_DOUBLE_AR_POUCH),
		new ACM_AreaRoutingMapping(ACM_Waist, ACM_EAreaFamily.WAIST),

		// --- Legs --------------------------------------------------------------
		new ACM_AreaRoutingMapping(ACM_L_Leg, ACM_EAreaFamily.LEG_LEFT),
		new ACM_AreaRoutingMapping(ACM_Leg_Pouch, ACM_EAreaFamily.LEG_POUCH),
		new ACM_AreaRoutingMapping(ACM_R_Leg, ACM_EAreaFamily.LEG_RIGHT),
		new ACM_AreaRoutingMapping(ACM_TQ_2, ACM_EAreaFamily.TOURNIQUET),

		// --- Backpack & bag ----------------------------------------------------
		new ACM_AreaRoutingMapping(ACM_Bag_Pouch_Setup, ACM_EAreaFamily.BAG_POUCH_SETUP),
		new ACM_AreaRoutingMapping(ACM_Bag_SlotArea_Bottom, ACM_EAreaFamily.BAG_SLOT_BOTTOM),
		new ACM_AreaRoutingMapping(ACM_Bag_SlotArea_Left, ACM_EAreaFamily.BAG_SLOT_LEFT),
		new ACM_AreaRoutingMapping(ACM_Bag_SlotArea_Right, ACM_EAreaFamily.BAG_SLOT_RIGHT),

		// --- Weapon carriage ---------------------------------------------------
		new ACM_AreaRoutingMapping(ACM_Sling, ACM_EAreaFamily.SLING)
	};

	// Resolve a LoadoutAreaType subclass to its declared family.
	// Returns ACM_EAreaFamily.NONE for unmapped or empty typenames.
	static ACM_EAreaFamily ResolveFamilyForArea(typename area)
	{
		if (area == typename.Empty)
			return ACM_EAreaFamily.NONE;

		foreach (ACM_AreaRoutingMapping mapping : ACM_AREA_TYPE_ROUTING_MAPPINGS)
		{
			if (!mapping)
				continue;
			if (mapping.m_AreaType == area)
				return mapping.m_Family;
		}

		return ACM_EAreaFamily.NONE;
	}

	// True if the area is declared in the registry (i.e. has a non-NONE family).
	static bool IsRoutableArea(typename area)
	{
		return ResolveFamilyForArea(area) != ACM_EAreaFamily.NONE;
	}

	// Symmetric family check. Returns true when both areas resolve to the same
	// non-NONE family, or when they are the same typename.
	static bool AreasShareFamily(typename a, typename b)
	{
		if (a == typename.Empty || b == typename.Empty)
			return false;
		if (a == b)
			return true;

		ACM_EAreaFamily fa = ResolveFamilyForArea(a);
		if (fa == ACM_EAreaFamily.NONE)
			return false;

		return fa == ResolveFamilyForArea(b);
	}
}
