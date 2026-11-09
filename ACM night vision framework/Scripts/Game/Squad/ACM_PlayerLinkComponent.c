//------------------------------------------------------------------------------------------------
// ACM_PlayerLinkComponent.c
// The owner-bound pipe between one player and the server for squad commands and tactical roles.
// Lives on the character prefab.
//
// WHY THIS COMPONENT EXISTS AT ALL
//
// A game-mode component is addressed by every client as the same instance, so Enfusion has no way to
// tell the server which client an RPC came from - the server would have to take the client's word
// for who it is, and a modified client could set somebody else's role or command somebody else's
// squad. A component on the character is bound to one connection. The server reads the player id off
// THIS component's own owner entity, so identity is derived rather than claimed.
//------------------------------------------------------------------------------------------------

[ComponentEditorProps(category: "ACM/Tactical", description: "Owner-bound link between a player and the server for squad commands and roles. Add to the character prefab.")]
class ACM_PlayerLinkComponentClass : ScriptComponentClass {}

class ACM_PlayerLinkComponent : ScriptComponent
{
	// The local player's own link, cached on the client.
	protected static ACM_PlayerLinkComponent s_LocalInstance;

	protected int m_iPlayerId = -1;
	protected bool m_bIsLocal;

	// NVG face glow: -1 off, otherwise the glow colour packed as 0xRRGGBB. One value, so on/off and
	// colour always arrive together (two properties could land in either order and fake a flash).
	[RplProp(onRplName: "OnFaceGlowReplicated")]
	protected int m_iFaceGlowState = -1;

	protected ref ACM_NVGFaceGlow m_FaceGlow;

	//------------------------------------------------------------------------------------------------
	static ACM_PlayerLinkComponent GetLocal()
	{
		return s_LocalInstance;
	}

	//------------------------------------------------------------------------------------------------
	// Server-side lookup of a given player's pipe.
	//------------------------------------------------------------------------------------------------
	static ACM_PlayerLinkComponent GetForPlayer(int playerId)
	{
		PlayerManager playerManager = GetGame().GetPlayerManager();
		if (!playerManager)
			return null;

		IEntity controlled = playerManager.GetPlayerControlledEntity(playerId);
		if (!controlled)
			return null;

		return ACM_PlayerLinkComponent.Cast(controlled.FindComponent(ACM_PlayerLinkComponent));
	}

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		SetEventMask(owner, EntityEvent.INIT);
	}

	//------------------------------------------------------------------------------------------------
	override void EOnInit(IEntity owner)
	{
		// Deferred: the player controller is not necessarily bound to the character at component
		// init, so resolving identity here would resolve it to nothing.
		GetGame().GetCallqueue().CallLater(ResolveIdentity, 500, false);

		// Late join / stream-in: show a glow that was already on before this character arrived.
		GetGame().GetCallqueue().CallLater(OnFaceGlowReplicated, 1500, false);
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		if (GetGame() && GetGame().GetCallqueue())
		{
			GetGame().GetCallqueue().Remove(ResolveIdentity);
			GetGame().GetCallqueue().Remove(OnFaceGlowReplicated);
		}

		m_FaceGlow = null;
		super.OnDelete(owner);
	}

	//------------------------------------------------------------------------------------------------
	// NVG FACE GLOW
	//
	// The wearer's night vision component asks; the server stores one replicated value; every machine
	// lights its own copy of the character. The wearer applies it at once rather than waiting for the
	// round trip.
	//------------------------------------------------------------------------------------------------
	//! Returns false if this link cannot send yet (identity not resolved), so the caller retries.
	bool AskSetFaceGlow(bool on, vector color)
	{
		if (!m_bIsLocal)
			return false;

		int state = PackGlow(on, color);
		ApplyFaceGlowState(state);

		if (!Replication.IsRunning())
		{
			m_iFaceGlowState = state;
			return true;
		}

		Rpc(Rpc_AskSetFaceGlow, state);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void Rpc_AskSetFaceGlow(int state)
	{
		if (state < -1 || state > 0xFFFFFF)
			return;

		m_iFaceGlowState = state;
		Replication.BumpMe();

		// The authority gets no property callback; a listen-server host still needs to see it.
		if (RplSession.Mode() != RplMode.Dedicated)
			ApplyFaceGlowState(state);
	}

	//------------------------------------------------------------------------------------------------
	protected void OnFaceGlowReplicated()
	{
		ApplyFaceGlowState(m_iFaceGlowState);
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyFaceGlowState(int state)
	{
		if (RplSession.Mode() == RplMode.Dedicated)
			return;

		if (state < 0)
		{
			if (m_FaceGlow)
				m_FaceGlow.Apply(false, vector.Zero);
			return;
		}

		if (!m_FaceGlow)
			m_FaceGlow = new ACM_NVGFaceGlow(GetOwner());

		m_FaceGlow.Apply(true, UnpackGlow(state));
	}

	//------------------------------------------------------------------------------------------------
	protected static int PackGlow(bool on, vector color)
	{
		if (!on)
			return -1;

		int r = Math.Clamp(Math.Round(color[0] * 255), 0, 255);
		int g = Math.Clamp(Math.Round(color[1] * 255), 0, 255);
		int b = Math.Clamp(Math.Round(color[2] * 255), 0, 255);
		return (r << 16) | (g << 8) | b;
	}

	//------------------------------------------------------------------------------------------------
	protected static vector UnpackGlow(int state)
	{
		int r = (state >> 16) & 0xFF;
		int g = (state >> 8) & 0xFF;
		int b = state & 0xFF;
		return Vector(r / 255.0, g / 255.0, b / 255.0);
	}

	//------------------------------------------------------------------------------------------------
	protected void ResolveIdentity()
	{
		IEntity owner = GetOwner();
		if (!owner)
			return;

		PlayerManager playerManager = GetGame().GetPlayerManager();
		if (playerManager)
			m_iPlayerId = playerManager.GetPlayerIdFromControlledEntity(owner);

		if (m_iPlayerId <= 0)
		{
			// AI characters carry this component too and simply never resolve a player. Retry for a
			// human whose controller has not attached yet.
			GetGame().GetCallqueue().CallLater(ResolveIdentity, 1000, false);
			return;
		}

		m_bIsLocal = (SCR_PlayerController.GetLocalControlledEntity() == owner);

		if (m_bIsLocal)
			s_LocalInstance = this;
	}

	//------------------------------------------------------------------------------------------------
	// ROLES
	//------------------------------------------------------------------------------------------------
	void RequestRole(ACM_ETacticalRole role)
	{
		if (!m_bIsLocal)
			return;

		if (!Replication.IsRunning())
		{
			ACM_TacticalRoleComponent registry = ACM_TacticalRoleComponent.GetInstance();
			if (registry && m_iPlayerId > 0)
				registry.ApplyRoleAuthoritative(m_iPlayerId, role);

			return;
		}

		Rpc(Rpc_AskSetRole, role);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void Rpc_AskSetRole(int role)
	{
		if (!ACM_TacticalRoleCatalog.IsValidRoleValue(role))
			return;

		ACM_TacticalRoleComponent registry = ACM_TacticalRoleComponent.GetInstance();
		if (!registry)
			return;

		int playerId = GetOwnerPlayerId();
		if (playerId <= 0)
			return;

		registry.ApplyRoleAuthoritative(playerId, role);
	}

	//------------------------------------------------------------------------------------------------
	// SQUAD COMMAND - client asks, server decides.
	//
	// None of these carry a "who am I" argument. Every one is validated again on arrival:
	// ACM_SquadCommandComponent re-derives group and leadership from live state and refuses anything
	// it does not like.
	//------------------------------------------------------------------------------------------------
	void AskSetMemberRole(int targetPlayerId, int role)
	{
		if (!m_bIsLocal)
			return;

		if (!Replication.IsRunning())
		{
			ServerSquadSetRole(GetOwnerPlayerId(), targetPlayerId, role);
			return;
		}

		Rpc(Rpc_AskSetMemberRole, targetPlayerId, role);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void Rpc_AskSetMemberRole(int targetPlayerId, int role)
	{
		ServerSquadSetRole(GetOwnerPlayerId(), targetPlayerId, role);
	}

	//------------------------------------------------------------------------------------------------
	void AskSetSquadSize(int size)
	{
		if (!m_bIsLocal)
			return;

		if (!Replication.IsRunning())
		{
			ServerSquadSetSize(GetOwnerPlayerId(), size);
			return;
		}

		Rpc(Rpc_AskSetSquadSize, size);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void Rpc_AskSetSquadSize(int size)
	{
		ServerSquadSetSize(GetOwnerPlayerId(), size);
	}

	//------------------------------------------------------------------------------------------------
	//! Rename the group. Leader-only, enforced on the server - this only asks.
	void AskSetGroupName(string name)
	{
		if (!m_bIsLocal)
			return;

		if (!Replication.IsRunning())
		{
			ServerSquadSetName(GetOwnerPlayerId(), name);
			return;
		}

		Rpc(Rpc_AskSetGroupName, name);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void Rpc_AskSetGroupName(string name)
	{
		ServerSquadSetName(GetOwnerPlayerId(), name);
	}

	//------------------------------------------------------------------------------------------------
	//! action: 0 open, 1 close, 2 request, 3 approve, 4 deny.
	void AskSquadLink(int action, int otherGroupId)
	{
		if (!m_bIsLocal)
			return;

		if (!Replication.IsRunning())
		{
			ServerSquadLink(GetOwnerPlayerId(), action, otherGroupId);
			return;
		}

		Rpc(Rpc_AskSquadLink, action, otherGroupId);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void Rpc_AskSquadLink(int action, int otherGroupId)
	{
		ServerSquadLink(GetOwnerPlayerId(), action, otherGroupId);
	}

	//------------------------------------------------------------------------------------------------
	// Server-side dispatch. Shared by the RPC path and the no-replication path so single player and
	// the editor take exactly the same route as a dedicated server.
	//------------------------------------------------------------------------------------------------
	protected void ServerSquadSetName(int callerPlayerId, string name)
	{
		ACM_SquadCommandComponent command = ACM_SquadCommandComponent.GetInstance();
		if (command)
			command.ServerSetGroupName(callerPlayerId, name);
	}

	//------------------------------------------------------------------------------------------------
	protected void ServerSquadSetRole(int callerPlayerId, int targetPlayerId, int role)
	{
		ACM_SquadCommandComponent command = ACM_SquadCommandComponent.GetInstance();
		if (command)
			command.ServerSetMemberRole(callerPlayerId, targetPlayerId, role);
	}

	//------------------------------------------------------------------------------------------------
	protected void ServerSquadSetSize(int callerPlayerId, int size)
	{
		ACM_SquadCommandComponent command = ACM_SquadCommandComponent.GetInstance();
		if (command)
			command.ServerSetSquadSize(callerPlayerId, size);
	}

	//------------------------------------------------------------------------------------------------
	protected void ServerSquadLink(int callerPlayerId, int action, int otherGroupId)
	{
		ACM_SquadCommandComponent command = ACM_SquadCommandComponent.GetInstance();
		if (!command)
			return;

		if (action == 0)
			command.ServerOpenLink(callerPlayerId, otherGroupId);
		else if (action == 1)
			command.ServerCloseLink(callerPlayerId, otherGroupId);
		else if (action == 2)
			command.ServerRequestLink(callerPlayerId, otherGroupId);
		else if (action == 3)
			command.ServerApproveLink(callerPlayerId, otherGroupId);
		else if (action == 4)
			command.ServerDenyLink(callerPlayerId, otherGroupId);
	}

	//------------------------------------------------------------------------------------------------
	//! The player this component's character belongs to, resolved on the SERVER from the entity the
	//! RPC arrived through. Never taken from the message.
	protected int GetOwnerPlayerId()
	{
		PlayerManager players = GetGame().GetPlayerManager();
		if (!players)
			return -1;

		return players.GetPlayerIdFromControlledEntity(GetOwner());
	}
}
