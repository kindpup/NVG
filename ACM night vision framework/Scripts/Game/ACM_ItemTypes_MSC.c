//
//  ACM
//  ACM Item Types
//
//  ACM Modular Framework implementation.
//==================================================================================================

/*!
	ACM
	Item Type Definitions

	Extends ECommonItemType with ACM-owned values only.
	The pocket rules layer aliases ACM and external item-type names so ACM can run
	standalone without colliding with third-party modular inventory frameworks.
*/

modded enum ECommonItemType
{
	// ACM chest-mounted digital planning devices.
	ACM_ATAK,
	// ACM communications equipment carried on rigs, belts, and back panels.
	ACM_RADIO,
	// ACM identification and admin items used by patch slots.
	ACM_PATCH,
	// ACM explosive family used by frag-oriented pouches and placards.
	ACM_GRENADE,
	// ACM smoke family used by signalling and concealment pouches.
	ACM_SMOKE_GRENADE,
	// ACM sidearm ammunition family for compact mag storage.
	ACM_PISTOL_AMMO,
	// ACM rifle magazine family for general-purpose placards and pouches.
	ACM_RIFLE_AMMO,
	// ACM precision rifle family for marksman placards.
	ACM_MARKSMAN_AMMO,
	// ACM support-weapon ammunition family for belt-fed or large mag carriage.
	ACM_MG_AMMO,
		// ACM support-weapon ammunition family for AK Styled Mags
	ACM_AK_AMMO,
			// ACM support-weapon ammunition family for GL Styled Weapons
	ACM_GL_AMMO,
	ACM_Pouch,

	// =========================================================================
	// AUTO-ROUTED + FIRST-PERSON HIDDEN section.
	//
	// Add new ECommonItemType values BETWEEN `ACM_AUTO_ROUTED_BEGIN` and
	// `ACM_AUTO_ROUTED_END`. Any value placed in this range is automatically:
	//   - Routed via ACM family system (ACM_EItemFamily.AUTO).
	//   - Hidden in first-person camera (alpha-faded like vanilla cloth items).
	//   - Included in the ACM policy router so explicit-target placements skip
	//     foreign containers and untargeted inserts find the right slot.
	//
	// Slot configuration still differentiates positions: a slot configured
	// with `m_ACM_MagCategory ACM_EAR_LEFT` only accepts items whose
	// CommonItemType (or m_ACM_AdditionalCommonItemTypes sidecar) is
	// ACM_EAR_LEFT, even though all auto-routed items share the AUTO family.
	//
	// `ACM_AUTO_ROUTED_BEGIN` and `ACM_AUTO_ROUTED_END` are sentinel markers
	// used internally for range detection. Do NOT assign them to any prefab.
	// =========================================================================
	ACM_AUTO_ROUTED_BEGIN,
	ACM_EAR_LEFT,
	ACM_EAR_RIGHT,
	ACM_EAR_LEFT_REAR_MOUNT,
	ACM_EAR_Right_REAR_MOUNT,
	ACM_NightVision,
	ACM_RailLiteRight,
	ACM_RailLiteLeft,
	ACM_Visor,
	ACM_AUTO_ROUTED_END
}