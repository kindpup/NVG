//------------------------------------------------------------------------------------------------
// ACM_RemoteSignalEvaluator.c
// Works out link quality. Nothing else - no widgets, no render targets.
//
// Authorisation happens before this runs. An unauthorised source is never evaluated, otherwise a
// link percentage tells you a camera exists that you are not allowed to know about.
//
// Evaluate() takes a path rather than a from/to pair so relays can be added later without
// changing every caller.
//------------------------------------------------------------------------------------------------

class ACM_RemoteSignalEvaluator
{
	// Per-frame evaluation would cost per-frame raycasts and look identical - the smoothing below
	// irons out the extra resolution anyway.
	static const int EVALUATE_INTERVAL_MS = 333;
	static const int EVALUATE_INTERVAL_LOST_MS = 1000;

	// Traces are the expensive bit, so they run slower still and the result is cached.
	static const int OBSTRUCTION_INTERVAL_MS = 500;

	// One ray gives a boolean. Several give a fraction, so a link can survive partial cover.
	// Ray count is the granularity: 3 rays can only report 0/33/66/100%, which steps visibly.
	static const int OBSTRUCTION_RAYS = 5;

	// Measured from eye height. A ray from the feet clips terrain and reports blocked while the
	// viewer is stood in the open.
	static const float VIEWER_EYE_HEIGHT_M = 1.6;
	static const float OBSTRUCTION_SPREAD_M = 1.2;

	// Smoothing rate. Without it the percentage jitters as traces flick between clear and blocked.
	static const float SMOOTHING_PER_SECOND = 2.5;

	//------------------------------------------------------------------------------------------------
	// Separate thresholds for dropping and recovering. One threshold makes a feed sat on the
	// cutoff strobe between dead and alive.
	//------------------------------------------------------------------------------------------------
	static const float LOSS_THRESHOLD = 0.05;
	static const float LOSS_DWELL_SECONDS = 1.5;
	static const float RECOVER_THRESHOLD = 0.12;
	static const float RECOVER_DWELL_SECONDS = 1.0;
	static const float RECONNECT_SECONDS = 0.75;

	//------------------------------------------------------------------------------------------------
	protected float m_fQuality;
	protected float m_fRawQuality;
	protected ACM_ERemoteFeedLinkState m_eState = ACM_ERemoteFeedLinkState.NONE;

	protected float m_fLastEvaluate;
	protected float m_fLastObstruction;
	protected float m_fObstructionFraction;

	protected float m_fBelowSince = -1;
	protected float m_fAboveSince = -1;
	protected float m_fReconnectingSince = -1;

	// For a future jammer. Subtracted only, so an unimplemented one can never improve a link.
	protected float m_fJammingLoss;

	// The receiving set's antenna. Both ends of the link matter.
	protected float m_fReceiverGain = 1.0;

	//------------------------------------------------------------------------------------------------
	float GetQuality()
	{
		return m_fQuality;
	}

	//------------------------------------------------------------------------------------------------
	ACM_ERemoteFeedLinkState GetState()
	{
		return m_eState;
	}

	//------------------------------------------------------------------------------------------------
	// Rounded here so every readout agrees.
	//------------------------------------------------------------------------------------------------
	float GetObstructionFraction()
	{
		return m_fObstructionFraction;
	}

	//------------------------------------------------------------------------------------------------
	int GetLinkPercent()
	{
		return Math.Round(m_fQuality * 100);
	}

	//------------------------------------------------------------------------------------------------
	// Nothing sets this yet.
	//------------------------------------------------------------------------------------------------
	void SetJammingLoss(float loss)
	{
		m_fJammingLoss = loss;
	}

	//------------------------------------------------------------------------------------------------
	void SetReceiverGain(float gain)
	{
		if (gain <= 0)
			gain = 1.0;

		m_fReceiverGain = gain;
	}

	//------------------------------------------------------------------------------------------------
	// Testing hook. Still runs the state machine so hysteresis and bands are exercised.
	//------------------------------------------------------------------------------------------------
	void ForceQuality(float quality, float nowSeconds)
	{
		m_fRawQuality = quality;
		m_fQuality = quality;

		UpdateState(nowSeconds);
	}

	//------------------------------------------------------------------------------------------------
	// Stops a newly selected source inheriting the last one's quality and dwell timers.
	//------------------------------------------------------------------------------------------------
	void Reset()
	{
		m_fQuality = 0;
		m_fRawQuality = 0;
		m_eState = ACM_ERemoteFeedLinkState.NONE;
		m_fLastEvaluate = 0;
		m_fLastObstruction = 0;
		m_fObstructionFraction = 0;
		m_fBelowSince = -1;
		m_fAboveSince = -1;
		m_fReconnectingSince = -1;
	}

	//------------------------------------------------------------------------------------------------
	// True when a re-evaluation actually happened, so callers can skip reapplying render settings.
	// The path holds one hop today but the loop already treats it as a chain.
	//------------------------------------------------------------------------------------------------
	bool Evaluate(vector viewerPos, notnull array<vector> path, ACM_RemoteFeedSourceComponent source, float nowMs)
	{
		if (!source)
		{
			Reset();
			return false;
		}

		int interval = EVALUATE_INTERVAL_MS;
		if (m_eState == ACM_ERemoteFeedLinkState.LOST)
			interval = EVALUATE_INTERVAL_LOST_MS;

		if (m_fLastEvaluate > 0 && nowMs - m_fLastEvaluate < interval)
			return false;

		float deltaSeconds = (nowMs - m_fLastEvaluate) / 1000.0;
		if (m_fLastEvaluate <= 0)
			deltaSeconds = interval / 1000.0;

		m_fLastEvaluate = nowMs;

		m_fRawQuality = Compute(viewerPos, path, source, nowMs);
		Smooth(deltaSeconds);
		UpdateState(nowMs / 1000.0);

		return true;
	}

	//------------------------------------------------------------------------------------------------
	// base - distance - obstruction - equipment + altitude - jamming, clamped.
	// Cheap disqualifications first, before anything spends a raycast.
	//------------------------------------------------------------------------------------------------
	protected float Compute(vector viewerPos, notnull array<vector> path, ACM_RemoteFeedSourceComponent source, float nowMs)
	{
		ACM_RemoteFeedProfile profile = source.GetLinkProfile();
		if (!profile)
			return 0;

		// Before any trace. An unpowered source is not a weak link, it is no link.
		if (profile.requiresPower && !source.IsPowered())
			return 0;

		vector sourcePos;
		if (!source.GetLinkOrigin(sourcePos))
			return 0;

		// Worst leg governs - a relay is never better than its weakest segment.
		float quality = 1.0;

		IEntity viewerEntity = SCR_PlayerController.GetLocalControlledEntity();
		IEntity sourceEntity = source.GetOwner();

		// From the eyes, not the boots.
		vector legStart = viewerPos + Vector(0, VIEWER_EYE_HEIGHT_M, 0);
		int hops = path.Count();

		for (int i = 0; i < hops; i++)
		{
			vector legEnd = path[i];
			float legQuality = EvaluateLeg(legStart, legEnd, profile, viewerEntity, sourceEntity);

			if (legQuality < quality)
				quality = legQuality;

			legStart = legEnd;
		}

		// Equipment scales whatever the geometry allowed.
		// Both ends of the link, not just the far one.
		quality = quality * profile.antennaGain * m_fReceiverGain;

		// Small bonus only - height already helped the obstruction result, so a big one would
		// count the same advantage twice.
		if (profile.altitudeHelps && profile.altitudeFullBonusMetres > 0)
		{
			float height = sourcePos[1] - viewerPos[1];
			if (height > 0)
			{
				float t = height / profile.altitudeFullBonusMetres;
				if (t > 1)
					t = 1;

				quality = quality + (profile.altitudeBonus * t);
			}
		}

		quality = quality - m_fJammingLoss;

		if (quality < 0)
			return 0;

		if (quality > 1)
			return 1;

		return quality;
	}

	//------------------------------------------------------------------------------------------------
	protected float EvaluateLeg(vector from, vector to, ACM_RemoteFeedProfile profile, IEntity viewerEntity, IEntity sourceEntity)
	{
		float distance = vector.Distance(from, to);

		float quality = DistanceFactor(distance, profile);
		if (quality <= 0)
			return 0;

		// Graded, not boolean. Treating the first branch in the way as a total loss makes every
		// ground source unusable in a town.
		float blocked = SampleObstruction(from, to, viewerEntity, sourceEntity);
		quality = quality - (blocked * profile.obstructionWeight * profile.sensitivity);

		if (quality < 0)
			return 0;

		return quality;
	}

	//------------------------------------------------------------------------------------------------
	// Full strength to optimal, curved decline to max, nothing beyond. A hard cutoff at 600m reads
	// as a bug rather than as physics.
	//------------------------------------------------------------------------------------------------
	protected float DistanceFactor(float distance, ACM_RemoteFeedProfile profile)
	{
		if (distance >= profile.maxRange)
			return 0;

		// Inside optimal range the link is good but not perfect. A flat 100% across all of normal
		// engagement range means the readout only moves when the source is nearly gone.
		float nearLoss = profile.nearFalloff * profile.sensitivity;
		if (nearLoss > 0.9)
			nearLoss = 0.9;

		if (distance <= profile.optimalRange)
		{
			if (profile.optimalRange <= 0)
				return 1.0 - nearLoss;

			float near = distance / profile.optimalRange;
			return 1.0 - (nearLoss * Math.Pow(near, profile.nearFalloffPower));
		}

		float span = profile.maxRange - profile.optimalRange;
		if (span <= 0)
			return 0;

		float t = (distance - profile.optimalRange) / span;

		// Picks up where the near curve left off.
		float remaining = 1.0 - nearLoss;
		return remaining * (1.0 - Math.Pow(t, profile.distanceFalloffPower));
	}

	//------------------------------------------------------------------------------------------------
	// Fraction of sample rays blocked, cached between samples. A boolean cannot tell a hedge from
	// a hill.
	//------------------------------------------------------------------------------------------------
	protected float SampleObstruction(vector from, vector to, IEntity viewerEntity, IEntity sourceEntity)
	{
		float nowMs = System.GetTickCount();
		if (m_fLastObstruction > 0 && nowMs - m_fLastObstruction < OBSTRUCTION_INTERVAL_MS)
			return m_fObstructionFraction;

		m_fLastObstruction = nowMs;

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return m_fObstructionFraction;

		vector along = to - from;
		float length = along.Length();
		if (length < 0.01)
		{
			m_fObstructionFraction = 0;
			return 0;
		}

		along = along / length;

		// Built by hand. Enfusion's vector * vector is not confirmed to be a cross product, and if
		// it is not, every offset ray lands on the centre ray and obstruction goes binary.
		vector side = Vector(-along[2], 0, along[0]);
		float sideLength = side.Length();
		if (sideLength < 0.01)
			side = Vector(1, 0, 0);
		else
			side = side / sideLength;

		vector up = Vector(0, 1, 0);

		int blocked = 0;

		for (int i = 0; i < OBSTRUCTION_RAYS; i++)
		{
			// Cross pattern. A horizontal-only spread misses a ledge.
			vector shift = vector.Zero;

			if (i == 1)
				shift = side * OBSTRUCTION_SPREAD_M;
			else if (i == 2)
				shift = side * -OBSTRUCTION_SPREAD_M;
			else if (i == 3)
				shift = up * OBSTRUCTION_SPREAD_M;
			else if (i == 4)
				shift = up * -OBSTRUCTION_SPREAD_M;

			if (!TraceClear(world, from + shift, to + shift, viewerEntity, sourceEntity))
				blocked++;
		}

		m_fObstructionFraction = blocked / OBSTRUCTION_RAYS;
		return m_fObstructionFraction;
	}

	//------------------------------------------------------------------------------------------------
	// WORLD | ENTS, same as the target detection traces. DEFAULT skips entity colliders, and in
	// Reforger buildings and vehicles are entities - it sails through a house and reports clear.
	//------------------------------------------------------------------------------------------------
	protected bool TraceClear(BaseWorld world, vector from, vector to, IEntity viewerEntity, IEntity sourceEntity)
	{
		TraceParam param = new TraceParam();
		param.Start = from;
		param.End = to;
		param.Flags = TraceFlags.WORLD | TraceFlags.ENTS | TraceFlags.ANY_CONTACT;
		param.LayerMask = EPhysicsLayerDefs.Default;

		// Both ends must be excluded or the ray hits the viewer's own collider and reports every
		// link fully blocked. On a body-worn camera that is every ray, every time.
		array<IEntity> exclude = {};
		if (viewerEntity)
			exclude.Insert(viewerEntity);
		if (sourceEntity)
			exclude.Insert(sourceEntity);

		param.ExcludeArray = exclude;

		return world.TraceMove(param, null) >= 0.999;
	}

	//------------------------------------------------------------------------------------------------
	// Exponential approach so the readout settles rather than chattering.
	//------------------------------------------------------------------------------------------------
	protected void Smooth(float deltaSeconds)
	{
		float t = SMOOTHING_PER_SECOND * deltaSeconds;
		if (t > 1)
			t = 1;

		m_fQuality = m_fQuality + ((m_fRawQuality - m_fQuality) * t);

		if (m_fQuality < 0)
			m_fQuality = 0;

		if (m_fQuality > 1)
			m_fQuality = 1;
	}

	//------------------------------------------------------------------------------------------------
	// Both transitions require the condition to hold, so one bad trace cannot drop a working feed.
	//------------------------------------------------------------------------------------------------
	protected void UpdateState(float nowSeconds)
	{
		if (m_eState == ACM_ERemoteFeedLinkState.NONE)
			m_eState = ACM_ERemoteFeedLinkState.LIVE;

		bool below = m_fQuality < LOSS_THRESHOLD;
		bool above = m_fQuality > RECOVER_THRESHOLD;

		if (below)
		{
			if (m_fBelowSince < 0)
				m_fBelowSince = nowSeconds;
		}
		else
		{
			m_fBelowSince = -1;
		}

		if (above)
		{
			if (m_fAboveSince < 0)
				m_fAboveSince = nowSeconds;
		}
		else
		{
			m_fAboveSince = -1;
		}

		if (m_eState == ACM_ERemoteFeedLinkState.LOST)
		{
			if (m_fAboveSince >= 0 && nowSeconds - m_fAboveSince >= RECOVER_DWELL_SECONDS)
			{
				m_eState = ACM_ERemoteFeedLinkState.RECONNECTING;
				m_fReconnectingSince = nowSeconds;
			}

			return;
		}

		if (m_eState == ACM_ERemoteFeedLinkState.RECONNECTING)
		{
			// Drop straight back rather than completing a handshake into a dead link.
			if (m_fBelowSince >= 0 && nowSeconds - m_fBelowSince >= LOSS_DWELL_SECONDS)
			{
				m_eState = ACM_ERemoteFeedLinkState.LOST;
				return;
			}

			if (nowSeconds - m_fReconnectingSince >= RECONNECT_SECONDS)
				m_eState = ACM_ERemoteFeedLinkState.LIVE;

			return;
		}

		if (m_fBelowSince >= 0 && nowSeconds - m_fBelowSince >= LOSS_DWELL_SECONDS)
			m_eState = ACM_ERemoteFeedLinkState.LOST;
	}
}
