//------------------------------------------------------------------------------------------------
// ACM_PlayerLinkComponent.c
// Owner-bound pipe between one player and the server. Lives on the character prefab.
//
// Carries the NVG eyecup face glow: the wearer asks, the server stores one replicated value, and
// every machine lights its own copy of the character. A component on the character is bound to one
// connection, so the server knows which player is asking without trusting the message.
//------------------------------------------------------------------------------------------------

[ComponentEditorProps(category: "ACM/Equipment", description: "Owner-bound link between a player and the server (NVG face glow). Add to the character prefab.")]
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
}
