//
//  ACM
//  ACM External Compatibility Aliases
//
//  ACM Modular Framework implementation.
//==================================================================================================

/*!
	External compatibility aliases.

	This file exists only to avoid item-type and routing collisions when third-party
	modular inventory frameworks are loaded alongside ACM.
	ACM logic normalizes external type names into ACM-owned policy categories and does
	not depend on external framework implementations.
*/
class ACM_ExternalCompatibilityAliases
{
	static string NormalizeExternalItemTypeName(string typeName)
	{
		switch (typeName)
		{
			case "GRS_RIFLE_AMMO":
				return "ACM_RIFLE_AMMO";

			case "GRS_MARKSMAN_AMMO":
				return "ACM_MARKSMAN_AMMO";

			case "GRS_MG_AMMO":
				return "ACM_MG_AMMO";

			case "GRS_PISTOL_AMMO":
				return "ACM_PISTOL_AMMO";

			case "GRS_SMOKE_GRENADE":
				return "ACM_SMOKE_GRENADE";

			case "GRS_FRAG_GRENADE":
			case "GRS_GRENADE":
				return "ACM_GRENADE";

			case "GRS_RADIO":
				return "ACM_RADIO";

			case "GRS_PATCH":
				return "ACM_PATCH";

			case "GRS_ATAK":
				return "ACM_ATAK";
		}

		return typeName;
	}

	protected static string GetExternalHolsterRouterTypeName()
	{
		// Runtime detection string intentionally kept here to isolate external symbol references.
		return "GRS_WeaponHolsterManagerComponent";
	}

	static bool HasExternalHolsterRouter(IEntity owner)
	{
		if (!owner)
			return false;

		string externalHolsterRouterTypeName = GetExternalHolsterRouterTypeName();
		typename externalHolsterRouterType = externalHolsterRouterTypeName.ToType();
		if (!externalHolsterRouterType)
			return false;

		return owner.FindComponent(externalHolsterRouterType) != null;
	}
}
