//------------------------------------------------------------------------------------------------
// ACM_TacticalRole.c
// Tactical role enum and the central role -> presentation catalog.
//
// A role is PLAYER-DECLARED FRIENDLY METADATA. It says what a friendly does, not where any
// information came from, and it must not be confused with ACM_TDContactSource - that describes
// the provenance of HOSTILE intelligence and has nothing to do with this file. A player can be a
// UAV_OPERATOR while a hostile contact arrives from LOCAL_VISUAL, and a contact can arrive from
// UAV while nobody present is a UAV_OPERATOR. The two enums never meet.
//
// Faction decides WHO is friendly. Role decides HOW that friendly is presented.
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
// Declared tactical role.
//
// UNASSIGNED is deliberately zero so an unset player defaults to it rather than to whichever role
// happened to be written first, and so a zeroed integer arriving over the wire is a safe value
// rather than a meaningful one.
//
// The renderer never switches on individual members of this enum - it asks the catalog below for
// a preset. Adding a role is therefore a matter of adding a member here and a default preset
// there, with no renderer change at all.
//------------------------------------------------------------------------------------------------
enum ACM_ETacticalRole
{
	UNASSIGNED,

	RIFLEMAN,
	AUTOMATIC_RIFLEMAN,
	GRENADIER,
	MARKSMAN,

	MEDIC,
	COMBAT_ENGINEER,
	RADIO_OPERATOR,
	JTAC,
	UAV_OPERATOR,

	TEAM_LEADER,
	SQUAD_LEADER,
	PLATOON_LEADER,

	CREW,
	PILOT,

	SPECIAL_OPERATIONS
}

//------------------------------------------------------------------------------------------------
// How one role is presented on the IVAS display.
//
// BaseContainerProps so an array of these can be authored on the registry component and shipped
// in the prefab: colours are meant to be retuned in Workbench without a rebuild and without the
// IFF renderer being touched.
//------------------------------------------------------------------------------------------------
[BaseContainerProps()]
class ACM_TacticalRolePreset
{
	[Attribute("0", UIWidgets.ComboBox, desc: "Role this preset describes.", enums: ParamEnumArray.FromEnum(ACM_ETacticalRole))]
	ACM_ETacticalRole m_eRole;

	[Attribute("1 1 1 1", UIWidgets.ColorPicker, desc: "Marker and text colour for this role. Both the glyph and the abbreviation inherit it, so colour and text are two independent identification channels carrying the same answer.")]
	ref Color m_Color;

	[Attribute("", UIWidgets.EditBox, desc: "Short abbreviation shown on the marker - MED, SL, JTAC. Keep it to four characters or fewer; it is drawn at marker scale next to a small glyph.")]
	string m_sAbbreviation;

	[Attribute("", UIWidgets.EditBox, desc: "Full role name, used by the DETAILED display density and by the role selection menu.")]
	string m_sDisplayName;

	[Attribute("", UIWidgets.EditBox, desc: "Reserved for a future imageset icon name. Phase 3 draws a coloured glyph plus the abbreviation and ignores this; it exists so real symbols can be added later without changing the role or network architecture.")]
	string m_sIconName;

	[Attribute("0", UIWidgets.Slider, desc: "Display priority. Higher-priority roles are drawn last so they land on top when markers overlap, and survive the friendly cap when more friendlies are visible than can be drawn. Leadership and specialist roles should outrank riflemen.", params: "0 100 1")]
	int m_iPriority;
}

//------------------------------------------------------------------------------------------------
// The one place role presentation is resolved.
//
// Built-in defaults live here so the system works out of the box with nothing authored; the
// registry component may override any subset of them from the prefab. Everything that draws a
// friendly asks this catalog, so retuning a colour is a prefab edit rather than a code change.
//------------------------------------------------------------------------------------------------
class ACM_TacticalRoleCatalog
{
	protected static ref array<ref ACM_TacticalRolePreset> s_aPresets;

	//------------------------------------------------------------------------------------------------
	static ACM_TacticalRolePreset GetPreset(ACM_ETacticalRole role)
	{
		EnsureBuilt();

		foreach (ACM_TacticalRolePreset preset : s_aPresets)
		{
			if (preset && preset.m_eRole == role)
				return preset;
		}

		// Never null: a role with no preset still has to draw as something rather than crash the
		// friendly pass.
		return s_aPresets[0];
	}

	//------------------------------------------------------------------------------------------------
	static array<ref ACM_TacticalRolePreset> GetAll()
	{
		EnsureBuilt();
		return s_aPresets;
	}

	//------------------------------------------------------------------------------------------------
	// Range check for a role integer arriving over the network. The server calls this before it
	// stores anything, so a malformed or out-of-range value is rejected rather than cached and
	// later handed to the renderer.
	//------------------------------------------------------------------------------------------------
	static bool IsValidRoleValue(int value)
	{
		return value >= ACM_ETacticalRole.UNASSIGNED && value <= ACM_ETacticalRole.SPECIAL_OPERATIONS;
	}

	//------------------------------------------------------------------------------------------------
	// Prefab-authored overrides, applied over the defaults by role. A partial list is fine - only
	// the roles present are replaced, everything else keeps its default.
	//------------------------------------------------------------------------------------------------
	static void ApplyOverrides(array<ref ACM_TacticalRolePreset> overrides)
	{
		if (!overrides || overrides.IsEmpty())
			return;

		EnsureBuilt();

		foreach (ACM_TacticalRolePreset over : overrides)
		{
			if (!over)
				continue;

			for (int i = 0; i < s_aPresets.Count(); i++)
			{
				if (s_aPresets[i].m_eRole != over.m_eRole)
					continue;

				// Field by field rather than wholesale, so an override that leaves the
				// abbreviation blank keeps the default one instead of blanking the marker.
				if (over.m_Color)
					s_aPresets[i].m_Color = over.m_Color;

				if (!over.m_sAbbreviation.IsEmpty())
					s_aPresets[i].m_sAbbreviation = over.m_sAbbreviation;

				if (!over.m_sDisplayName.IsEmpty())
					s_aPresets[i].m_sDisplayName = over.m_sDisplayName;

				if (!over.m_sIconName.IsEmpty())
					s_aPresets[i].m_sIconName = over.m_sIconName;

				if (over.m_iPriority > 0)
					s_aPresets[i].m_iPriority = over.m_iPriority;

				break;
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static void EnsureBuilt()
	{
		if (s_aPresets && !s_aPresets.IsEmpty())
			return;

		s_aPresets = {};

		// UNASSIGNED first, and it is the fallback in more than one sense: GetPreset returns it for
		// any role it cannot resolve, and the renderer substitutes the device's own friendly colour
		// for its colour, so a player who has declared nothing looks exactly like the plain friendly
		// marker the system drew before roles existed.
		Add(ACM_ETacticalRole.UNASSIGNED,          0.60, 0.85, 0.60, "",     "Unassigned",          0);

		Add(ACM_ETacticalRole.RIFLEMAN,            0.35, 0.85, 0.35, "RFL",  "Rifleman",           10);
		Add(ACM_ETacticalRole.AUTOMATIC_RIFLEMAN,  0.20, 0.65, 0.25, "AR",   "Automatic Rifleman", 25);
		Add(ACM_ETacticalRole.GRENADIER,           0.55, 0.75, 0.25, "GRN",  "Grenadier",          20);
		Add(ACM_ETacticalRole.MARKSMAN,            0.30, 0.80, 0.60, "DM",   "Marksman",           30);

		Add(ACM_ETacticalRole.MEDIC,               0.80, 1.00, 1.00, "MED",  "Medic",              65);
		Add(ACM_ETacticalRole.COMBAT_ENGINEER,     0.90, 0.60, 0.20, "ENG",  "Combat Engineer",    50);
		Add(ACM_ETacticalRole.RADIO_OPERATOR,      0.40, 0.75, 0.95, "RTO",  "Radio Operator",     55);
		Add(ACM_ETacticalRole.JTAC,                0.70, 0.45, 0.95, "JTAC", "JTAC",               70);
		Add(ACM_ETacticalRole.UAV_OPERATOR,        0.30, 0.55, 1.00, "UAV",  "UAV Operator",       60);

		Add(ACM_ETacticalRole.TEAM_LEADER,         1.00, 0.90, 0.30, "TL",   "Team Leader",        80);
		Add(ACM_ETacticalRole.SQUAD_LEADER,        1.00, 0.65, 0.20, "SL",   "Squad Leader",       90);
		Add(ACM_ETacticalRole.PLATOON_LEADER,      1.00, 0.45, 0.25, "PL",   "Platoon Leader",    100);

		Add(ACM_ETacticalRole.CREW,                0.75, 0.60, 0.40, "CRW",  "Crew",               40);
		Add(ACM_ETacticalRole.PILOT,               0.60, 0.85, 1.00, "PIL",  "Pilot",              45);

		Add(ACM_ETacticalRole.SPECIAL_OPERATIONS,  0.90, 0.35, 0.75, "SOF",  "Special Operations", 35);
	}

	//------------------------------------------------------------------------------------------------
	protected static void Add(ACM_ETacticalRole role, float r, float g, float b, string abbreviation, string displayName, int priority)
	{
		ACM_TacticalRolePreset preset = new ACM_TacticalRolePreset();
		preset.m_eRole = role;
		preset.m_Color = Color.FromRGBA(Math.Round(r * 255), Math.Round(g * 255), Math.Round(b * 255), 255);
		preset.m_sAbbreviation = abbreviation;
		preset.m_sDisplayName = displayName;
		preset.m_sIconName = "";
		preset.m_iPriority = priority;

		s_aPresets.Insert(preset);
	}
}
