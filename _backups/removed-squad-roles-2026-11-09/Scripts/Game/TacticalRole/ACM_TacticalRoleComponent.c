//------------------------------------------------------------------------------------------------
// ACM_TacticalRoleComponent.c
// Server-authoritative registry of player-declared tactical roles. Lives on the ACM-owned
// GameMode prefab.
//
// WHY THE GAME MODE, and not a character or the player controller:
//
//   - A role has to survive death, respawn, character replacement and gear changes for the
//     length of the session. The game mode outlives all of those; a character component would be
//     destroyed and rebuilt every time the player died, taking the role with it.
//   - playerId is the natural key for something that describes a PLAYER rather than a body, and
//     the game mode is where a playerId-keyed table belongs.
//   - The NVG mod deliberately does not declare `modded class SCR_PlayerController`, so that
//     there is no load-order-dependent chain to get wrong when Shadow Admin - which does mod it -
//     is installed alongside. That rules out the otherwise obvious home.
//   - ACM_NVL_NetworkComponent was the other candidate and is the wrong shape: it is character
//     scoped, knows nothing about players, exposes no getters for other clients to read, and its
//     JIP stream is a hand-rolled binary format with no version tag that already carries scar
//     tissue from being appended to once.
//
// THE ROLE API IS NOT OWNED BY THE MENU. RequestSetTacticalRole / GetTacticalRole /
// GetLocalTacticalRole are the whole interface, and the radial menu is merely the first caller.
// A mission framework, a loadout system, a squad leader assigning roles, an admin override or a
// server preset all call the same three methods. Nothing about roles lives in menu code.
//
// SEPARATION: a role is friendly metadata. It is NOT a contact source. ACM_TDContactSource
// describes where hostile intelligence came from and is a different enum in a different file for
// a different purpose. They are never combined, converted or compared.
//------------------------------------------------------------------------------------------------

[ComponentEditorProps(category: "ACM/Tactical", description: "Server-authoritative tactical role registry. Add to the game mode prefab.")]
class ACM_TacticalRoleComponentClass : SCR_BaseGameModeComponentClass {}

class ACM_TacticalRoleComponent : SCR_BaseGameModeComponent
{
	[Attribute("Medikit,MedicalKit,FirstAidKit", UIWidgets.EditBox, desc: "Comma-separated prefab-name fragments that identify a medical kit, used ONLY by the fallback inference for players who have declared no role. Matching is case-insensitive substring. Deliberately not a component test: every soldier carries bandages, so a component check would mark an entire squad as medics. Clear this to disable medic inference entirely.", category: "Fallback Inference")]
	protected string m_sMedicalKeywords;

	[Attribute("Radio_Backpack,RadioBackpack,LongRangeRadio", UIWidgets.EditBox, desc: "Comma-separated prefab-name fragments that identify a long-range radio, used ONLY by fallback inference. Kept narrow on purpose - a personal radio is standard issue and says nothing about role, so only backpack-class sets should be listed here. Clear this to disable radio-operator inference.", category: "Fallback Inference")]
	protected string m_sRadioKeywords;

	[Attribute(desc: "Optional role presentation overrides. Any role listed here replaces the built-in default for that role; roles left out keep theirs. This is the supported way to retune IFF colours and abbreviations without a rebuild.", category: "Role Presentation")]
	protected ref array<ref ACM_TacticalRolePreset> m_aRoleOverrides;

	protected static ACM_TacticalRoleComponent s_Instance;

	// playerId -> role, as an int. The enum is flattened for the wire because that is this mod's
	// established idiom - it has no RplProp anywhere and every enum it has ever replicated went
	// across as a plain int - and because an int is what the server range-checks on arrival.
	protected ref map<int, int> m_mRoles = new map<int, int>();

	// Parsed once at init rather than split per lookup - inference runs per friendly per refresh.
	protected ref array<string> m_aMedicalKeywords = {};
	protected ref array<string> m_aRadioKeywords = {};

	//------------------------------------------------------------------------------------------------
	static ACM_TacticalRoleComponent GetInstance()
	{
		return s_Instance;
	}

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		s_Instance = this;

		ACM_TacticalRoleCatalog.ApplyOverrides(m_aRoleOverrides);

		ParseKeywords(m_sMedicalKeywords, m_aMedicalKeywords);
		ParseKeywords(m_sRadioKeywords, m_aRadioKeywords);
	}

	//------------------------------------------------------------------------------------------------
	protected void ParseKeywords(string source, array<string> target)
	{
		target.Clear();

		if (source.IsEmpty())
			return;

		array<string> parts = {};
		source.Split(",", parts, true);

		foreach (string part : parts)
		{
			string trimmed = part;
			trimmed.Trim();

			if (!trimmed.IsEmpty())
				target.Insert(trimmed);
		}
	}

	//------------------------------------------------------------------------------------------------
	// PUBLIC API
	//
	// Three methods, and every future role-assignment system uses these rather than reaching into
	// the table. Keeping the surface this small is what lets a squad leader assignment, a mission
	// framework and the radial menu all be frontends over one authoritative value.
	//------------------------------------------------------------------------------------------------

	//------------------------------------------------------------------------------------------------
	// Ask the server to set the local player's role. Client-side entry point.
	//------------------------------------------------------------------------------------------------
	void RequestSetTacticalRole(ACM_ETacticalRole role)
	{
		if (!ACM_TacticalRoleCatalog.IsValidRoleValue(role))
			return;

		// Routed through the player's OWN character rather than sent straight here.
		//
		// This component lives on the game mode, which every client addresses as the same instance,
		// so Enfusion cannot tell the server which client an RPC came from - the old path had to
		// take a playerId as an argument and believe it, which meant a modified client could set
		// somebody else's role. The link component is bound to one connection, so the server reads
		// the id off its owner entity and the forgeable argument no longer exists.
		//
		// The signature here is unchanged, so the radial menu never learns any of this happened.
		ACM_PlayerLinkComponent link = ACM_PlayerLinkComponent.GetLocal();
		if (link)
		{
			link.RequestRole(role);
			return;
		}

		// No link component - single player, or a character that has not finished initialising.
		// Apply locally so the menu still works offline; with replication running this is a no-op
		// on a client because the registry only accepts committed values.
		if (!Replication.IsRunning())
		{
			PlayerController controller = GetGame().GetPlayerController();
			if (controller && controller.GetPlayerId() > 0)
				ApplyRoleAuthoritative(controller.GetPlayerId(), role);
		}
	}

	//------------------------------------------------------------------------------------------------
	// SERVER-SIDE ONLY. The hook for a future squad-leader assignment, mission framework, admin
	// override or server preset - all of which run on the server and therefore already know who they
	// are talking about.
	//
	// A client calling this does nothing, deliberately. There is no client path to setting another
	// player's role, and that is the whole point of the change above.
	//------------------------------------------------------------------------------------------------
	void ServerSetTacticalRole(int playerId, ACM_ETacticalRole role)
	{
		if (Replication.IsRunning() && !Replication.IsServer())
			return;

		if (playerId <= 0 || !ACM_TacticalRoleCatalog.IsValidRoleValue(role))
			return;

		ApplyRoleAuthoritative(playerId, role);
	}

	//------------------------------------------------------------------------------------------------
	// The role a given player has declared. Any client may ask about any player - this is friendly
	// metadata, not privileged information.
	//------------------------------------------------------------------------------------------------
	ACM_ETacticalRole GetTacticalRole(int playerId)
	{
		int role;
		if (playerId > 0 && m_mRoles.Find(playerId, role) && ACM_TacticalRoleCatalog.IsValidRoleValue(role))
			return role;

		return ACM_ETacticalRole.UNASSIGNED;
	}

	//------------------------------------------------------------------------------------------------
	// The role to DISPLAY for a friendly. This is what the IFF renderer asks for, and it is the
	// only place declared and inferred roles meet.
	//
	//   declared != UNASSIGNED  ->  the declared role, always. Manual selection wins outright, and
	//                               nothing about a player's equipment can override what they said
	//                               about themselves.
	//   otherwise               ->  a conservative guess from what this client can see of them,
	//                               falling back to RIFLEMAN when the evidence is not clear.
	//
	// The result is NEVER written back to the registry. A player who has declared nothing still
	// reads as UNASSIGNED to GetTacticalRole, still replicates as UNASSIGNED, and still shows an
	// empty selection in the menu - because they have in fact chosen nothing. What changes is only
	// how they are drawn, and that decision is made independently on each viewing client from the
	// entity in front of it. That is why inference costs no network traffic: there is nothing to
	// agree about, since every client looking at the same person sees the same evidence.
	//------------------------------------------------------------------------------------------------
	ACM_ETacticalRole GetEffectiveTacticalRole(int playerId, IEntity entity)
	{
		ACM_ETacticalRole declared = GetTacticalRole(playerId);
		if (declared != ACM_ETacticalRole.UNASSIGNED)
			return declared;

		return ACM_TacticalRoleInference.Infer(entity, m_aMedicalKeywords, m_aRadioKeywords);
	}

	//------------------------------------------------------------------------------------------------
	// Static form of the resolver above, and the one the IFF renderer calls.
	//
	// It exists because the registry is a component on a game mode prefab, and a world whose active
	// game mode does not carry that component has no instance for the renderer to ask. Before this,
	// that case fell all the way through to UNASSIGNED and every friendly drew in the same flat
	// fallback colour - which looks exactly like role colouring being broken, when in fact the roles
	// were never being resolved at all.
	//
	// Without an instance a DECLARED role cannot exist - there is nowhere to store it and nothing to
	// replicate it - but inference is client-local and needs neither, so friendlies still colour by
	// their inferred role. Degraded, not dead.
	//------------------------------------------------------------------------------------------------
	static ACM_ETacticalRole ResolveEffectiveRole(int playerId, IEntity entity)
	{
		if (s_Instance)
			return s_Instance.GetEffectiveTacticalRole(playerId, entity);

		return ACM_TacticalRoleInference.Infer(entity, null, null);
	}

	//------------------------------------------------------------------------------------------------
	static bool HasRegistry()
	{
		return s_Instance != null;
	}

	//------------------------------------------------------------------------------------------------
	ACM_ETacticalRole GetLocalTacticalRole()
	{
		PlayerController controller = GetGame().GetPlayerController();
		if (!controller)
			return ACM_ETacticalRole.UNASSIGNED;

		return GetTacticalRole(controller.GetPlayerId());
	}

	//------------------------------------------------------------------------------------------------
	// NETWORKING
	//------------------------------------------------------------------------------------------------

	//------------------------------------------------------------------------------------------------
	// Server-side commit. Every accepted change funnels through here, whether it arrived over the
	// wire or was made locally on a listen server, so there is one place where the table changes.
	//------------------------------------------------------------------------------------------------
	void ApplyRoleAuthoritative(int playerId, int role)
	{
		m_mRoles.Set(playerId, role);

		if (Replication.IsRunning())
			Rpc(Rpc_DoSetRole, playerId, role);
	}

	//------------------------------------------------------------------------------------------------
	// Server -> clients. Note there is no NoOwner condition here: unlike the light RPCs this mod
	// models these on, EVERY client needs this value, including the one that asked for it, because
	// each client caches the whole table rather than just applying a local effect.
	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void Rpc_DoSetRole(int playerId, int role)
	{
		if (playerId <= 0 || !ACM_TacticalRoleCatalog.IsValidRoleValue(role))
			return;

		m_mRoles.Set(playerId, role);
	}

	//------------------------------------------------------------------------------------------------
	// JIP, belt and braces.
	//
	// RplSave/RplLoad below is the proper mechanism and mirrors what ACM_NVL_NetworkComponent does.
	// This broadcast on connect exists because that path is easy to have silently not fire on a
	// game-mode component, and a role table that quietly fails to reach a joiner would show up as
	// "everyone is UNASSIGNED for the new guy" - a bug that looks like a display fault and would be
	// hunted in entirely the wrong file. The table is a handful of ints and this runs once per
	// connection, so the redundancy costs nothing worth measuring.
	//------------------------------------------------------------------------------------------------
	override void OnPlayerConnected(int playerId)
	{
		super.OnPlayerConnected(playerId);

		if (!Replication.IsRunning() || !Replication.IsServer())
			return;

		if (m_mRoles.IsEmpty())
			return;

		// Re-broadcast entry by entry rather than shipping the table as an array. RPC argument
		// serialisation of arrays is not something this codebase has ever relied on, and the table
		// is bounded by the server's player count - a handful of int pairs, once per connection.
		// Not worth being clever about, and being clever here fails silently.
		for (int i = 0; i < m_mRoles.Count(); i++)
			Rpc(Rpc_DoSetRole, m_mRoles.GetKey(i), m_mRoles.GetElement(i));
	}

	//------------------------------------------------------------------------------------------------
	// A disconnecting player's role is dropped. Roles last for the session, and a stale entry would
	// otherwise be inherited by whoever the engine next hands that playerId to.
	//------------------------------------------------------------------------------------------------
	override void OnPlayerDisconnected(int playerId, KickCauseCode cause, int timeout)
	{
		super.OnPlayerDisconnected(playerId, cause, timeout);

		if (Replication.IsRunning() && !Replication.IsServer())
			return;

		m_mRoles.Remove(playerId);
	}

	//------------------------------------------------------------------------------------------------
	override bool RplSave(ScriptBitWriter writer)
	{
		writer.WriteInt(m_mRoles.Count());

		for (int i = 0; i < m_mRoles.Count(); i++)
		{
			writer.WriteInt(m_mRoles.GetKey(i));
			writer.WriteInt(m_mRoles.GetElement(i));
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	override bool RplLoad(ScriptBitReader reader)
	{
		int count;
		if (!reader.ReadInt(count))
			return false;

		for (int i = 0; i < count; i++)
		{
			int playerId;
			int role;

			if (!reader.ReadInt(playerId))
				return false;

			if (!reader.ReadInt(role))
				return false;

			// Validated on the way in as well as on the way out. A corrupt or out-of-date stream
			// must not be able to seed the table with a role value the catalog cannot resolve.
			if (playerId > 0 && ACM_TacticalRoleCatalog.IsValidRoleValue(role))
				m_mRoles.Set(playerId, role);
		}

		return true;
	}
}
