//------------------------------------------------------------------------------------------------
// ACM_TacticalContactManager.c
// The authority for shared tactical contacts. Game-mode component; server-authoritative.
//
// Sensors produce information, this owns contacts, the HUD displays them, and the drone feed - when
// it exists - displays video and knows nothing about any of this. That separation is the whole
// design and this file is the middle of it.
//
// WHAT THIS CLASS PROTECTS
//
// Replication.FindItem(RplId) is public and returns the live object, so a hostile's replicated id
// is functionally an entity handle. The rplId -> contactId map below is the only place the two are
// associated, it lives on the server, and it is never published. Receivers are told a contactId,
// which resolves to nothing anywhere else.
//
// The one exception is deliberate and safe: the REPORTER is acknowledged with the binding for the
// target it just reported. It already holds that entity - it saw it - so the binding tells it
// nothing new, and it is what lets a client merge its own contact with the shared one instead of
// drawing the same enemy twice.
//------------------------------------------------------------------------------------------------

[ComponentEditorProps(category: "ACM/Tactical", description: "Server-authoritative tactical contact store for the IVAS data link. Add to the game mode prefab.")]
class ACM_TacticalContactManagerClass : SCR_BaseGameModeComponentClass {}

class ACM_TacticalContactManager : SCR_BaseGameModeComponent
{
	//------------------------------------------------------------------------------------------------
	// Validation
	//------------------------------------------------------------------------------------------------
	[Attribute("0.35", UIWidgets.Slider, desc: "Fraction of accepted reports that receive a full server-side line-of-sight trace. Sampling rather than mandatory verification: position plausibility already makes fabrication useless, so this exists to make wall-reporting risky rather than free. 1.0 is equivalent to strict mode.", category: "Validation", params: "0 1 0.05")]
	protected float m_fLosAuditRate;

	[Attribute("0", UIWidgets.CheckBox, desc: "Audit every single report instead of sampling. Correct but expensive - a trace per report per reporter per scan is the one cost that does not survive a full server. Overrides the audit rate above.", category: "Validation")]
	protected bool m_bStrictLinkLos;

	[Attribute("0.34", UIWidgets.Slider, desc: "Audit failure rate above which a reporter is suspended. A client failing a third of its audits is not unlucky.", category: "Validation", params: "0.05 1 0.01")]
	protected float m_fLosFailureThreshold;

	[Attribute("8", UIWidgets.Slider, desc: "Minimum audits before the failure rate is acted on, so a player who happens to fail their first trace is not suspended for it.", category: "Validation", params: "3 50 1")]
	protected int m_iLosMinAudits;

	[Attribute("30", UIWidgets.Slider, desc: "Seconds a reporter is suspended from the link after failing the audit threshold.", category: "Validation", params: "5 600 5")]
	protected float m_fSuspendSeconds;

	[Attribute("25", UIWidgets.Slider, desc: "Metres a reported position may differ from where the server knows the target actually is. This is the strongest gate in the chain - the server has ground truth, so a fabricated position cannot survive it. Tolerance covers movement between the client's scan and the server's check.", category: "Validation", params: "5 100 1")]
	protected float m_fPositionTolerance;

	[Attribute("1.25", UIWidgets.Slider, desc: "Multiplier applied to the reporting device's maximum range before rejecting a report as out of range, covering movement and scan latency.", category: "Validation", params: "1 3 0.05")]
	protected float m_fRangeTolerance;

	//------------------------------------------------------------------------------------------------
	// Budgets
	//------------------------------------------------------------------------------------------------
	[Attribute("6", UIWidgets.Slider, desc: "Sustained reports per second per player.", category: "Budgets", params: "1 30 1")]
	protected float m_fReportsPerSecond;

	[Attribute("12", UIWidgets.Slider, desc: "Burst capacity per player. Lets a spotter report a squad walking into view at once - which is when the link matters most - without allowing a sustained stream.", category: "Budgets", params: "1 60 1")]
	protected float m_fReportBurst;

	[Attribute("2.5", UIWidgets.Slider, desc: "Metres a contact must move before its new position is delivered again. A contact whose position has not meaningfully changed costs nothing after its first delivery.", category: "Budgets", params: "0.5 25 0.5")]
	protected float m_fRedeliverDistance;

	[Attribute("5", UIWidgets.Slider, desc: "Seconds after which a contact is redelivered even if it has not moved, so ages stay roughly in step across clients.", category: "Budgets", params: "1 60 1")]
	protected float m_fRedeliverInterval;

	[Attribute("64", UIWidgets.Slider, desc: "Maximum player-controlled hostile contacts tracked per faction.", category: "Budgets", params: "8 256 8")]
	protected int m_iMaxPlayerContacts;

	[Attribute("96", UIWidgets.Slider, desc: "Maximum AI hostile contacts tracked per faction. Budgeted separately from player contacts on purpose: a firefight against thirty AI would otherwise evict the handful of player contacts that matter most, purely on arrival order.", category: "Budgets", params: "0 512 8")]
	protected int m_iMaxAiContacts;

	[Attribute("45", UIWidgets.Slider, desc: "Seconds after last confirmation before a shared contact is dropped from the table entirely.", category: "Budgets", params: "5 600 5")]
	protected float m_fContactLifetime;

	//------------------------------------------------------------------------------------------------
	// Policy
	//------------------------------------------------------------------------------------------------
	[Attribute("0", UIWidgets.ComboBox, desc: "How widely a reported contact is shared. Squad only is the default and the narrowest useful setting: a faction-wide link means every contact anyone spots is instantly known to the whole side, which removes most of the reason to talk to each other. Platoon is reserved and currently behaves as squad only.", category: "Policy", enums: ParamEnumArray.FromEnum(ACM_TDLinkScope))]
	protected ACM_TDLinkScope m_eLinkScope;

	[Attribute("1", UIWidgets.CheckBox, desc: "Allow AI-controlled hostiles onto the data link at all. Off, only player-controlled hostiles are shared and AI remain a purely local matter.", category: "Policy")]
	protected bool m_bShareAiContacts;

	//------------------------------------------------------------------------------------------------
	protected static ACM_TacticalContactManager s_Instance;

	// contactId -> contact. Server-side truth.
	protected ref map<int, ref ACM_TDLinkContact> m_mContacts = new map<int, ref ACM_TDLinkContact>();

	// THE SECRET MAP. targetRplId -> contactId. Server only, never published, never serialised into
	// anything a client receives. This is what makes contactId opaque rather than a rename.
	protected ref map<int, int> m_mRplToContact = new map<int, int>();

	// playerId -> rate budget.
	protected ref map<int, ref ACM_TDLinkBudget> m_mBudgets = new map<int, ref ACM_TDLinkBudget>();

	protected int m_iNextContactId = 1;

	// Rolling counter for audit sampling. Deterministic rather than random: Math.Random is not
	// available to scripts here and a counter is auditable anyway.
	protected int m_iReportCounter;

	protected float m_fNextSweep;

	// Explicit cross-group grants. Server-only, and there is deliberately no RPC that writes to it.
	protected ref array<ref ACM_TDLinkGrant> m_aGrants = {};

	//------------------------------------------------------------------------------------------------
	static ACM_TacticalContactManager GetInstance()
	{
		return s_Instance;
	}

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		s_Instance = this;
		if (!SCR_Global.IsEditMode() && (!Replication.IsRunning() || Replication.IsServer()))
			ACM_IffServerSettings.ServerMask();


		// Server-only maintenance tick. Expiry has to happen somewhere, and a game-mode component
		// does not tick on its own; 2s is far finer than the shortest sensible contact lifetime.
		if (!Replication.IsRunning() || Replication.IsServer())
			GetGame().GetCallqueue().CallLater(SweepTick, 2000, true);
	}

	//------------------------------------------------------------------------------------------------
	protected void SweepTick()
	{
		Sweep(System.GetTickCount() / 1000.0);
	}

	//------------------------------------------------------------------------------------------------
	// INGEST
	//
	// Called ONLY from ACM_PlayerLinkComponent's server-side RPC handler, which has already derived
	// reporterPlayerId from the owner entity of the component the RPC arrived on. This method never
	// takes a player id from a client, and there is deliberately no overload that would let it.
	//------------------------------------------------------------------------------------------------
	ACM_TDRejectReason SubmitReport(int reporterPlayerId, IEntity reporterEntity, int targetRplId, vector reportedPos, ACM_TDSensor sensor, out int outContactId)
	{
		outContactId = -1;

		float now = System.GetTickCount() / 1000.0;

		// G1 - reporter is real. Derived, never claimed.
		if (reporterPlayerId <= 0 || !reporterEntity)
			return ACM_TDRejectReason.NO_REPORTER;

		// G2 - rate. Cheapest meaningful rejection, so it runs before anything that costs a lookup.
		if (!ConsumeBudget(reporterPlayerId, now))
			return ACM_TDRejectReason.RATE_LIMITED;

		// G3 - capability, and the range comes from here.
		//
		// The client used to send its own detection range for the server to compare against, so a
		// modified client could claim any range and report every hostile streamed to it. Position
		// plausibility still blocked fabricated positions, but unlimited range is most of the value
		// of the link on a large map. Resolved from the device server-side now.
		//
		// Unchanged limit: whether the goggles are switched on is not server-authoritative, so this
		// proves the reporter carries a device that could have seen it, not that it was lit.
		ACM_TargetDetectionComponent device = FindDetectionDevice(reporterEntity);
		if (!device || !device.SharesContacts())
			return ACM_TDRejectReason.NO_CAPABILITY;

		float reporterRange = device.GetMaxRange();
		if (reporterRange <= 0)
			return ACM_TDRejectReason.NO_CAPABILITY;

		// G4 - target exists. This is also the ONLY place a client-supplied rplId is resolved, and
		// the result never leaves this method's scope as anything but validation input.
		//
		// No sign test on targetRplId here either - see the note on ACM_TDContact.targetId. A valid
		// id is negative as an int, so rejecting negatives would reject everything.
		if (targetRplId == Replication.INVALID_ID)
			return ACM_TDRejectReason.TARGET_MISSING;

		IEntity target = IEntity.Cast(Replication.FindItem(targetRplId));
		if (!target)
			return ACM_TDRejectReason.TARGET_MISSING;

		// G5 - hostility, resolved server-side from both factions. The reporter's opinion is not
		// consulted, so a client cannot promote a friendly into a hostile contact.
		Faction reporterFaction = SCR_Faction.GetEntityFaction(reporterEntity);
		Faction targetFaction = SCR_Faction.GetEntityFaction(target);
		if (!reporterFaction || !targetFaction)
			return ACM_TDRejectReason.NOT_HOSTILE;

		if (!reporterFaction.IsFactionEnemy(targetFaction))
			return ACM_TDRejectReason.NOT_HOSTILE;

		// G6 - sensor range.
		vector truePos = target.GetOrigin();
		float reporterDistance = vector.Distance(reporterEntity.GetOrigin(), truePos);
		if (reporterDistance > reporterRange * m_fRangeTolerance)
			return ACM_TDRejectReason.OUT_OF_RANGE;

		// G7 - position plausibility. The strong one: the server knows where the target actually is,
		// so a fabricated position dies here regardless of how the client dressed it up.
		if (vector.Distance(reportedPos, truePos) > m_fPositionTolerance)
			return ACM_TDRejectReason.POSITION_IMPLAUSIBLE;

		// G8 - sampled line of sight.
		if (ShouldAudit())
		{
			bool clear = AuditLineOfSight(reporterEntity, target);
			if (!RecordAuditResult(reporterPlayerId, clear, now))
				return ACM_TDRejectReason.LOS_AUDIT_FAILED;
		}

		ACM_TDContactClass classification = ClassifyTarget(target);

		if (classification == ACM_TDContactClass.INFANTRY_AI && !m_bShareAiContacts)
			return ACM_TDRejectReason.NOT_HOSTILE;

		ACM_TDLinkContact contact = ResolveContact(targetRplId, classification, reporterFaction.GetFactionKey());
		if (!contact)
			return ACM_TDRejectReason.TABLE_FULL;

		// Accepted. Note what is stored and what is not: the true position from the SERVER, not the
		// reported one. A report that survived G7 is close enough to be believed, but there is no
		// reason to store the client's number when the authoritative one is right here.
		contact.lastKnownPos = truePos;
		contact.lastConfirmedTime = now;
		contact.sensor = sensor;
		contact.reporterPlayerId = reporterPlayerId;
		contact.classification = classification;

		// The reporting group earns the contact permanently, not just for this update.
		contact.AddContributingGroup(ResolveGroupId(reporterPlayerId));

		outContactId = contact.contactId;

		DeliverContact(contact, now, false);

		return ACM_TDRejectReason.ACCEPTED;
	}

	//------------------------------------------------------------------------------------------------
	// DEDUP
	//
	// One target is one contact, whoever reports it and whatever sensor they used. A hostile already
	// tracked from a drone and then seen by a soldier refreshes the existing record rather than
	// producing a second marker beside the first.
	//------------------------------------------------------------------------------------------------
	protected ACM_TDLinkContact ResolveContact(int targetRplId, ACM_TDContactClass classification, string factionKey)
	{
		int existingId;
		if (m_mRplToContact.Find(targetRplId, existingId))
		{
			ACM_TDLinkContact existing;
			if (m_mContacts.Find(existingId, existing))
				return existing;

			// Map entry outlived its contact - repair rather than leak.
			m_mRplToContact.Remove(targetRplId);
		}

		if (IsTableFull(classification, factionKey))
			return null;

		ACM_TDLinkContact created = new ACM_TDLinkContact(m_iNextContactId, targetRplId);
		created.ownerFactionKey = factionKey;
		m_iNextContactId++;

		m_mContacts.Set(created.contactId, created);
		m_mRplToContact.Set(targetRplId, created.contactId);

		return created;
	}

	//------------------------------------------------------------------------------------------------
	// Caps are per faction AND per class. AI pressure cannot starve player intelligence, because the
	// two populations never draw from the same budget.
	//------------------------------------------------------------------------------------------------
	protected bool IsTableFull(ACM_TDContactClass classification, string factionKey)
	{
		bool ai = (classification == ACM_TDContactClass.INFANTRY_AI);

		int limit = m_iMaxPlayerContacts;
		if (ai)
			limit = m_iMaxAiContacts;

		int count = 0;
		for (int i = 0; i < m_mContacts.Count(); i++)
		{
			ACM_TDLinkContact c = m_mContacts.GetElement(i);
			if (c && c.ownerFactionKey == factionKey && c.IsAiContact() == ai)
				count++;
		}

		return count >= limit;
	}

	//------------------------------------------------------------------------------------------------
	// DELIVERY
	//
	// Per receiver, down that receiver's own owner-bound link component. There is no broadcast to
	// filter, which is the point: a broadcast-then-filter design would put every enemy position on
	// every client and rely on client code not to look at it.
	//------------------------------------------------------------------------------------------------
	protected void DeliverContact(ACM_TDLinkContact contact, float now, bool force)
	{
		if (!force && !NeedsDelivery(contact, now))
			return;

		contact.lastDeliveredPos = contact.lastKnownPos;
		contact.lastDeliveredTime = now;

		PlayerManager playerManager = GetGame().GetPlayerManager();
		if (!playerManager)
			return;

		array<int> playerIds = {};
		playerManager.GetPlayers(playerIds);

		int delivered = 0;
		int refused = 0;

		foreach (int playerId : playerIds)
		{
			if (!CanReceiveContact(playerId, contact, now))
			{
				refused++;
				continue;
			}

			ACM_PlayerLinkComponent link = ACM_PlayerLinkComponent.GetForPlayer(playerId);
			if (!link)
			{
				refused++;
				continue;
			}

			link.PushContact(contact, now);
			delivered++;
		}

		Print(string.Format("[ACM Link] SERVER deliver contact %1 (faction=%2, scope=%3, groups=%4): %5 sent, %6 refused, %7 online",
			contact.contactId, contact.ownerFactionKey,
			SCR_Enum.GetEnumName(ACM_TDLinkScope, m_eLinkScope),
			contact.contributingGroups.Count(),
			delivered, refused, playerIds.Count()), LogLevel.DEBUG);
	}

	//------------------------------------------------------------------------------------------------
	// Deltas only. A static contact costs one delivery and then nothing.
	//------------------------------------------------------------------------------------------------
	protected bool NeedsDelivery(ACM_TDLinkContact contact, float now)
	{
		if (contact.lastDeliveredTime < 0)
			return true;

		if ((now - contact.lastDeliveredTime) >= m_fRedeliverInterval)
			return true;

		return vector.Distance(contact.lastKnownPos, contact.lastDeliveredPos) >= m_fRedeliverDistance;
	}

	//------------------------------------------------------------------------------------------------
	// JIP. Called by a link component once its owning client is ready.
	//
	// Deliberately NOT RplSave/RplLoad. That path streams the same payload to every connection, and
	// this payload is faction-scoped - a JIP dump is precisely where a broadcast-shaped mechanism
	// would hand every contact to the wrong side. The role table can use RplSave because a role is
	// public information; a contact is not.
	//------------------------------------------------------------------------------------------------
	void SyncToReceiver(ACM_PlayerLinkComponent link, int receiverPlayerId)
	{
		if (!link)
			return;

		float now = System.GetTickCount() / 1000.0;

		for (int i = 0; i < m_mContacts.Count(); i++)
		{
			ACM_TDLinkContact contact = m_mContacts.GetElement(i);
			if (!contact)
				continue;

			if (IsExpired(contact, now))
				continue;

			// The SAME eligibility test the live path uses, and this is the important part.
			//
			// A JIP dump is exactly where scope filtering gets forgotten, because it is a different
			// code path that happens once and is easy to test only with a single squad. Getting it
			// wrong hands a joining player every contact on the server in one burst - a leak that is
			// silent, total, and only visible to the person who benefits from it.
			if (!CanReceiveContact(receiverPlayerId, contact, now))
				continue;

			// Real ages, not fresh ones. A contact that went stale before this player connected
			// arrives already faded and expires on schedule rather than looking like new intelligence.
			link.PushContact(contact, now);
		}
	}

	//------------------------------------------------------------------------------------------------
	// ELIGIBILITY
	//
	// THE one place that decides whether a given player may see a given contact, used by both the
	// live delivery path and the JIP sync. Everything about scope funnels through here so there is a
	// single answer to audit rather than two that can drift apart.
	//
	// Runs entirely on the server and gates DELIVERY, not display. A contact a player is not
	// entitled to is never sent to their machine at all, so there is nothing on the client to hide,
	// nothing to filter, and nothing a modified client can reveal.
	//------------------------------------------------------------------------------------------------
	bool CanReceiveContact(int receiverPlayerId, ACM_TDLinkContact contact, float now)
	{
		if (receiverPlayerId <= 0 || !contact)
			return false;

		IEntity receiver = GetGame().GetPlayerManager().GetPlayerControlledEntity(receiverPlayerId);
		if (!receiver)
			return false;

		// 1. Same faction. Cheapest and the most absolute - nothing below can override it.
		Faction receiverFaction = SCR_Faction.GetEntityFaction(receiver);
		if (!receiverFaction || receiverFaction.GetFactionKey() != contact.ownerFactionKey)
			return false;

		// 2 & 3. Capability and consent, both read from the receiver's actual device on the server's
		// copy of the character rather than taken on the client's word. A player with no
		// data-link-capable device, or one with receiving switched off, is not sent contacts - which
		// also means they cost nothing to have online.
		ACM_TargetDetectionComponent device = FindDetectionDevice(receiver);
		if (!device || !device.ReceivesContacts())
			return false;

		// 4. Scope.
		return ScopePermits(receiverPlayerId, contact, now);
	}

	//------------------------------------------------------------------------------------------------
	protected bool ScopePermits(int receiverPlayerId, ACM_TDLinkContact contact, float now)
	{
		if (m_eLinkScope == ACM_TDLinkScope.FACTION)
			return true;

		int receiverGroup = ResolveGroupId(receiverPlayerId);

		// No group means no shared picture, and this is a decision rather than an oversight.
		//
		// The alternative - treating "ungrouped" as a group - would quietly pool every unassigned
		// player on the server into one shared feed, which is the widest possible sharing arrived at
		// by accident. A player who wants the link joins a squad.
		if (receiverGroup < 0)
			return false;

		if (contact.WasReportedByGroup(receiverGroup))
			return true;

		// CUSTOM adds explicit grants on top of squad scope; it never replaces it.
		if (m_eLinkScope == ACM_TDLinkScope.CUSTOM)
			return IsGrantedToGroup(contact, receiverGroup, now);

		// SQUAD_ONLY, and PLATOON until a parent-formation concept exists to implement it against.
		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsGrantedToGroup(ACM_TDLinkContact contact, int receiverGroup, float now)
	{
		foreach (int producerGroup : contact.contributingGroups)
		{
			foreach (ACM_TDLinkGrant grant : m_aGrants)
			{
				if (grant && !grant.IsExpired(now) && grant.Covers(producerGroup, receiverGroup))
					return true;
			}
		}

		return false;
	}

	// The remote feed's EXPLICIT_LINK policy asks this, so feed access and contact sharing share
	// one set of grants rather than two lists that drift apart.
	//------------------------------------------------------------------------------------------------
	bool GroupsAreLinked(int fromPlayerId, int toPlayerId)
	{
		int fromGroup = ResolveGroupId(fromPlayerId);
		int toGroup = ResolveGroupId(toPlayerId);

		if (fromGroup <= 0 || toGroup <= 0)
			return false;

		if (fromGroup == toGroup)
			return true;

		float now = System.GetTickCount() / 1000.0;

		foreach (ACM_TDLinkGrant grant : m_aGrants)
		{
			if (grant && !grant.IsExpired(now) && grant.Covers(fromGroup, toGroup))
				return true;
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	// EXPLICIT LINKS
	//
	// Server-side entry points only. There is no RPC that reaches these, and there must never be
	// one: a client able to grant itself a link to another squad's feed would make every scope
	// setting above decorative. An admin tool, a mission script or a future command interface calls
	// these directly, all of which already run on the server.
	//------------------------------------------------------------------------------------------------
	void ServerGrantLink(int fromGroupId, int toGroupId, bool bidirectional, float durationSeconds)
	{
		if (Replication.IsRunning() && !Replication.IsServer())
			return;

		if (fromGroupId < 0 || toGroupId < 0 || fromGroupId == toGroupId)
			return;

		float expiry = -1;
		if (durationSeconds > 0)
			expiry = (System.GetTickCount() / 1000.0) + durationSeconds;

		// Re-granting an existing pair refreshes it rather than stacking duplicates.
		foreach (ACM_TDLinkGrant existing : m_aGrants)
		{
			if (existing && existing.fromGroupId == fromGroupId && existing.toGroupId == toGroupId)
			{
				existing.bidirectional = bidirectional;
				existing.expiresAt = expiry;
				return;
			}
		}

		m_aGrants.Insert(new ACM_TDLinkGrant(fromGroupId, toGroupId, bidirectional, expiry));

		Print(string.Format("[ACM Link] grant issued: group %1 -> %2 (bidirectional=%3)", fromGroupId, toGroupId, bidirectional), LogLevel.DEBUG);
	}

	//------------------------------------------------------------------------------------------------
	void ServerRevokeLink(int fromGroupId, int toGroupId)
	{
		if (Replication.IsRunning() && !Replication.IsServer())
			return;

		for (int i = m_aGrants.Count() - 1; i >= 0; i--)
		{
			ACM_TDLinkGrant grant = m_aGrants[i];
			if (grant && grant.fromGroupId == fromGroupId && grant.toGroupId == toGroupId)
				m_aGrants.Remove(i);
		}
	}

	//------------------------------------------------------------------------------------------------
	// GROUP IDENTITY
	//
	// One resolver, so reporter and receiver are always identified the same way. Returns -1 for a
	// player with no group, which every caller above treats as "no shared picture".
	//------------------------------------------------------------------------------------------------
	protected int ResolveGroupId(int playerId)
	{
		if (playerId <= 0)
			return -1;

		SCR_GroupsManagerComponent groups = SCR_GroupsManagerComponent.GetInstance();
		if (!groups)
			return -1;

		SCR_AIGroup group = groups.GetPlayerGroup(playerId);
		if (!group)
			return -1;

		return group.GetGroupID();
	}

	//------------------------------------------------------------------------------------------------
	// Finds the detection device somewhere in a character's hierarchy. Server-side, reading the
	// server's own copy of the character, so capability and consent are facts rather than claims.
	//------------------------------------------------------------------------------------------------
	// Worn gear is not a child entity - it sits in an inventory slot, so a hierarchy walk alone
	// finds nothing on a character and every report gets rejected for having no device.
	//------------------------------------------------------------------------------------------------
	protected ACM_TargetDetectionComponent FindDetectionDevice(IEntity entity)
	{
		if (!entity)
			return null;

		ACM_TargetDetectionComponent device = ACM_TargetDetectionComponent.Cast(entity.FindComponent(ACM_TargetDetectionComponent));
		if (device)
			return device;

		// Searches the entity and everything slotted into it, which is where a helmet-mounted
		// device actually lives.
		device = ACM_TargetDetectionComponent.Cast(SCR_EntityHelper.FindComponent(entity, ACM_TargetDetectionComponent));
		if (device)
			return device;

		IEntity child = entity.GetChildren();
		while (child)
		{
			device = FindDetectionDevice(child);
			if (device)
				return device;

			child = child.GetSibling();
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	// MAINTENANCE
	//------------------------------------------------------------------------------------------------
	void Sweep(float now)
	{
		if (now < m_fNextSweep)
			return;

		m_fNextSweep = now + 2.0;

		array<int> stale = {};

		for (int i = 0; i < m_mContacts.Count(); i++)
		{
			ACM_TDLinkContact contact = m_mContacts.GetElement(i);
			if (contact && IsExpired(contact, now))
				stale.Insert(contact.contactId);
		}

		foreach (int contactId : stale)
		{
			ACM_TDLinkContact contact;
			if (!m_mContacts.Find(contactId, contact))
				continue;

			// Both maps, always together. A surviving rplId entry would silently rebind a future
			// contact to a dead id.
			m_mRplToContact.Remove(contact.targetRplId);
			m_mContacts.Remove(contactId);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsExpired(ACM_TDLinkContact contact, float now)
	{
		if (contact.lastConfirmedTime < 0)
			return true;

		return (now - contact.lastConfirmedTime) > m_fContactLifetime;
	}

	//------------------------------------------------------------------------------------------------
	// AUDITING
	//------------------------------------------------------------------------------------------------
	protected bool ShouldAudit()
	{
		if (m_bStrictLinkLos || m_fLosAuditRate >= 1.0)
			return true;

		if (m_fLosAuditRate <= 0)
			return false;

		m_iReportCounter++;

		// Deterministic sampling: audit every Nth report, where N falls out of the configured rate.
		int interval = Math.Round(1.0 / m_fLosAuditRate);
		if (interval < 1)
			interval = 1;

		return (m_iReportCounter % interval) == 0;
	}

	//------------------------------------------------------------------------------------------------
	// The same trace the client ran. WORLD | ENTS rather than DEFAULT for the reason documented in
	// the detection system: DEFAULT skips entity colliders, Reforger buildings are entities, and a
	// default trace would pass through a house and confirm every wall report it was meant to catch.
	//------------------------------------------------------------------------------------------------
	protected bool AuditLineOfSight(IEntity reporter, IEntity target)
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return true;

		vector from = reporter.GetOrigin() + Vector(0, 1.5, 0);
		vector to = target.GetOrigin() + Vector(0, 1.1, 0);

		TraceParam param = new TraceParam();
		param.Start = from;
		param.End = to;
		param.Flags = TraceFlags.WORLD | TraceFlags.ENTS | TraceFlags.ANY_CONTACT;
		param.LayerMask = EPhysicsLayerDefs.Default;

		array<IEntity> exclude = {};
		exclude.Insert(reporter);
		exclude.Insert(target);
		param.ExcludeArray = exclude;

		return world.TraceMove(param, null) >= 0.999;
	}

	//------------------------------------------------------------------------------------------------
	// Returns false when the reporter has just been suspended.
	//
	// Failures are not cleared by a single pass. A client failing a third of its audits is not
	// unlucky, and letting one clean report launder that would make the whole sample meaningless.
	//------------------------------------------------------------------------------------------------
	protected bool RecordAuditResult(int playerId, bool clear, float now)
	{
		ACM_TDLinkBudget budget;
		if (!m_mBudgets.Find(playerId, budget))
			return true;

		budget.losAudits++;

		if (clear)
			return true;

		budget.losFailures++;

		if (budget.losAudits < m_iLosMinAudits)
			return false;

		float rate = budget.losFailures / budget.losAudits;
		if (rate < m_fLosFailureThreshold)
			return false;

		budget.suspendedUntil = now + m_fSuspendSeconds;
		budget.losFailures = 0;
		budget.losAudits = 0;

		Print(string.Format("[ACM Link] player %1 suspended from the data link for %2s - sampled LOS failure rate %3", playerId, m_fSuspendSeconds, rate), LogLevel.WARNING);

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected bool ConsumeBudget(int playerId, float now)
	{
		ACM_TDLinkBudget budget;
		if (!m_mBudgets.Find(playerId, budget))
		{
			budget = new ACM_TDLinkBudget(m_fReportBurst, now);
			m_mBudgets.Set(playerId, budget);
		}

		return budget.TryConsume(now, m_fReportBurst, m_fReportsPerSecond);
	}

	//------------------------------------------------------------------------------------------------
	protected ACM_TDContactClass ClassifyTarget(IEntity target)
	{
		if (!ChimeraCharacter.Cast(target))
			return ACM_TDContactClass.VEHICLE;

		PlayerManager playerManager = GetGame().GetPlayerManager();
		if (playerManager && playerManager.GetPlayerIdFromControlledEntity(target) > 0)
			return ACM_TDContactClass.INFANTRY_PLAYER;

		return ACM_TDContactClass.INFANTRY_AI;
	}

	//------------------------------------------------------------------------------------------------
	override void OnPlayerDisconnected(int playerId, KickCauseCode cause, int timeout)
	{
		super.OnPlayerDisconnected(playerId, cause, timeout);

		m_mBudgets.Remove(playerId);
	}
}
