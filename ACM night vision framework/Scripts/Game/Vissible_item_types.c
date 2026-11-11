// =============================================================================
//                            ACM STUDIO FRAMEWORK
// -----------------------------------------------------------------------------
// ACM First-Person Hide Item Type List
//
// Items are auto-hidden in first-person when their `ECommonItemType`
// (or `m_ACM_AdditionalCommonItemTypes` sidecar) sits in the auto-routed
// sentinel range defined in `ACM ItemTypes MSC.c` (between
// `ACM_AUTO_ROUTED_BEGIN` and `ACM_AUTO_ROUTED_END`).
//
// To add a new FP-hidden type, just add the enum value to that range. The
// same range also drives ACM family routing and policy-router inclusion, so
// no further edits are required in this file or anywhere else.
//
// `ACM_FP_HIDE_EXTRA_TYPES` below is a manual override knob for engine-native
// or third-party types that cannot be added to the sentinel range (e.g. the
// engine's built-in `ECommonItemType.NIGHT_VISION`).
// =============================================================================

class ACM_FirstPersonHideTypes
{
	//------------------------------------------------------------------------------------------------
	//! Manual override list for types outside the ACM auto-routed sentinel range
	//! (e.g. engine-native `NIGHT_VISION`). Sentinel-range types are auto-detected
	//! via `ACM_ItemClassificationService.ACM_ItemHasAutoRoutedType`.
	static const ref array<ECommonItemType> ACM_FP_HIDE_EXTRA_TYPES = {
		ECommonItemType.NIGHT_VISION
	};

	//------------------------------------------------------------------------------------------------
	//! Returns true if the item should fade out in first-person camera.
	static bool ShouldHideInFirstPerson(IEntity item)
	{
		if (!item)
			return false;

		if (ACM_ItemClassificationService.ACM_ItemHasAutoRoutedType(item))
			return true;

		foreach (ECommonItemType type : ACM_FP_HIDE_EXTRA_TYPES)
		{
			if (ACM_ItemClassificationService.ItemMatchesCommonType(item, type))
				return true;
		}
		return false;
	}
};

