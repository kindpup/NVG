// ACM Casualty Vitals
//
// ONE INTERFACE, TWO SOURCES.
//
// The IVAS medical readout has to work on a plain Reforger server and on one running an advanced
// medical mod, and the UI must not know which. So everything the panel needs is expressed as a
// SNAPSHOT, and a PROVIDER fills it in from whatever medical model is actually installed.
//
// The vanilla provider reads what Reforger genuinely exposes: the blood hitzone, per-hitzone damage
// and the bleeding damage-over-time map. An advanced provider registers itself at startup and takes
// precedence when present, filling the same snapshot with measured values plus whatever extra it
// models.
//
// WHAT IS MEASURED AND WHAT IS INFERRED, AND WHY THE DIFFERENCE IS RECORDED.
//
// Reforger has no cardiac or respiratory model. There is no heart rate to read. Rather than leave
// two permanently blank rows on a medical panel, the vanilla provider DERIVES both from state it
// really does have - blood volume, total bleed rate and life state - and sets m_bDerived so the
// display can mark them as estimates.
//
// That flag is not decoration. A number a medic might act on should say whether it was measured or
// guessed, and an IVAS that presents an inferred pulse identically to a real one is lying to the
// person wearing it. When an advanced provider supplies genuine values it clears the flag and the
// same rows render without the qualifier.
//
// NO HARD DEPENDENCY ON ANY MEDICAL MOD.
//
// Nothing here references an advanced medical class, so this addon compiles and runs with none
// installed. The bridge is inverted: a compatibility addon subclasses ACM_VitalsProvider and calls
// ACM_VitalsService.Register() from its own init. If it is not loaded, nothing registers, the
// vanilla provider stays selected, and no call site changes.

enum ACM_EVitalState
{
	HEALTHY,
	INJURED,
	UNCONSCIOUS,
	DEAD
}

//------------------------------------------------------------------------------------------------
// HOW BADLY, as distinct from WHAT STATE.
//
// State answers "can this person fight" - healthy, injured, out, gone. Severity answers "how long
// have I got", which is the triage question and a different one. A casualty can be conscious and
// FATAL, or unconscious and only MODERATE with a tourniquet on.
//
// FATAL means alive but not survivable without immediate intervention. DEAD is a state, not a
// severity, and the two are deliberately not the same value.
//------------------------------------------------------------------------------------------------
enum ACM_EInjurySeverity
{
	NONE,
	MINOR,
	MODERATE,
	SEVERE,
	FATAL
}

//------------------------------------------------------------------------------------------------
//! One damaged or bleeding body part.
class ACM_VitalInjury
{
	string zone;			//!< Hitzone name as the medical model calls it.
	float damage;			//!< 0 = untouched, 1 = destroyed.
	float bleedRate;		//!< Damage per second from this zone. 0 when not bleeding.

	void ACM_VitalInjury(string zoneName, float dmg, float bleed)
	{
		zone = zoneName;
		damage = dmg;
		bleedRate = bleed;
	}
}

//------------------------------------------------------------------------------------------------
//! Everything the readout can show about one casualty. Fields carry their own "do I have this"
//! flag rather than a sentinel value, so a provider that cannot supply something says so instead
//! of the UI having to guess whether 0 means zero or means unknown.
class ACM_VitalsSnapshot
{
	bool valid;

	ACM_EVitalState state;
	ACM_EInjurySeverity severity;

	bool hasBlood;
	float bloodFraction;		//!< 1 = full, 0 = exsanguinated.

	bool hasBleeding;
	float bleedRate;			//!< Total, all zones.

	bool hasHeartRate;
	int heartRate;

	bool hasRespiration;
	int breathsPerMinute;

	//! True when heartRate/breathsPerMinute were inferred rather than measured.
	bool derived;

	//! True when an advanced medical model produced this snapshot.
	bool advanced;

	ref array<ref ACM_VitalInjury> injuries;

	void ACM_VitalsSnapshot()
	{
		injuries = new array<ref ACM_VitalInjury>();
	}

	void Reset()
	{
		valid = false;
		state = ACM_EVitalState.HEALTHY;
		severity = ACM_EInjurySeverity.NONE;
		hasBlood = false;
		bloodFraction = 1.0;
		hasBleeding = false;
		bleedRate = 0;
		hasHeartRate = false;
		heartRate = 0;
		hasRespiration = false;
		breathsPerMinute = 0;
		derived = false;
		advanced = false;
		injuries.Clear();
	}

	//! Severity on its own, for the details panel where both are shown.
	string SeverityLabel()
	{
		switch (severity)
		{
			case ACM_EInjurySeverity.FATAL:		return "FATAL";
			case ACM_EInjurySeverity.SEVERE:	return "SEVERE";
			case ACM_EInjurySeverity.MODERATE:	return "MODERATE";
			case ACM_EInjurySeverity.MINOR:		return "MINOR";
		}

		return "NONE";
	}

	//! The single most urgent word for the roster row.
	//!
	//! Death and unconsciousness outrank the grade because they change what you DO - you stop
	//! treating and start dragging. Below that the grade is what matters, so an injured casualty
	//! reads MINOR through FATAL rather than a flat "INJURED" that tells a medic nothing.
	string StateLabel()
	{
		switch (state)
		{
			case ACM_EVitalState.DEAD:			return "DEAD";
			case ACM_EVitalState.UNCONSCIOUS:	return "UNCON";
			case ACM_EVitalState.INJURED:		return SeverityLabel();
		}

		return "HEALTHY";
	}
}

//------------------------------------------------------------------------------------------------
//! Base provider. Subclass this in a compatibility addon and register it to take over.
class ACM_VitalsProvider
{
	//! Higher wins. Vanilla is 0 so anything real outranks it without having to know its number.
	int GetPriority()
	{
		return 0;
	}

	string GetName()
	{
		return "base";
	}

	//! Can this provider serve requests right now? A bridge can return false until its own systems
	//! are up, and the service will fall back rather than return empty snapshots.
	bool IsAvailable()
	{
		return true;
	}

	//! Fill the snapshot. Return false when the entity is not a readable casualty at all.
	bool Read(IEntity character, notnull ACM_VitalsSnapshot snapshot)
	{
		return false;
	}
}

//------------------------------------------------------------------------------------------------
// VANILLA.
//
// Reads SCR_CharacterDamageManagerComponent. Everything it reports as measured really is measured;
// the two cardiac rows are the only inferred values and they are flagged as such.
//------------------------------------------------------------------------------------------------
class ACM_VanillaVitalsProvider : ACM_VitalsProvider
{
	// Resting values for an uninjured soldier, and the ceilings a dying one approaches. Blood loss
	// drives tachycardia and air hunger, which is why both climb as bloodFraction falls.
	protected static const int HR_REST = 70;
	protected static const int HR_MAX = 160;
	protected static const int HR_UNCONSCIOUS = 40;

	protected static const int RR_REST = 14;
	protected static const int RR_MAX = 36;
	protected static const int RR_UNCONSCIOUS = 8;

	override int GetPriority()
	{
		return 0;
	}

	override string GetName()
	{
		return "vanilla";
	}

	//------------------------------------------------------------------------------------------------
	override bool Read(IEntity character, notnull ACM_VitalsSnapshot snapshot)
	{
		snapshot.Reset();

		if (!character)
			return false;

		SCR_CharacterDamageManagerComponent damage = SCR_CharacterDamageManagerComponent.Cast(SCR_CharacterDamageManagerComponent.GetDamageManager(character));
		if (!damage)
			return false;

		snapshot.valid = true;

		ReadLifeState(character, snapshot);
		ReadBlood(damage, snapshot);
		ReadInjuries(damage, snapshot);

		// INJURED is inferred from findings rather than asked for directly: a casualty is injured if
		// anything is bleeding, blood is down, or a body part is damaged. Death and unconsciousness
		// are read from the life state above and are not overridden here.
		if (snapshot.state == ACM_EVitalState.HEALTHY)
		{
			if (snapshot.bleedRate > 0 || !snapshot.injuries.IsEmpty() || snapshot.bloodFraction < 0.99)
				snapshot.state = ACM_EVitalState.INJURED;
		}

		GradeSeverity(snapshot);
		DeriveCardiac(snapshot);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	// Resolved with FindComponent, and death asked via IsDead().
	//
	// The first version went through ChimeraCharacter.GetCharacterController() and compared the life
	// state against DEAD. It silently produced nothing - an incapacitated, bleeding casualty read
	// back HEALTHY with a resting pulse. This is the pattern the rest of this mod already uses for
	// exactly this question (see ResolveFriendlyStatus), and it is known to work, so the readout now
	// asks the way everything else asks rather than inventing a second route to the same answer.
	protected void ReadLifeState(IEntity character, notnull ACM_VitalsSnapshot snapshot)
	{
		SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(character.FindComponent(SCR_CharacterControllerComponent));
		if (!controller)
			return;

		if (controller.IsDead())
		{
			snapshot.state = ACM_EVitalState.DEAD;
			return;
		}

		if (controller.IsUnconscious() || controller.GetLifeState() == ECharacterLifeState.INCAPACITATED)
			snapshot.state = ACM_EVitalState.UNCONSCIOUS;
	}

	//------------------------------------------------------------------------------------------------
	protected void ReadBlood(notnull SCR_CharacterDamageManagerComponent damage, notnull ACM_VitalsSnapshot snapshot)
	{
		SCR_CharacterBloodHitZone blood = damage.GetBloodHitZone();
		if (!blood)
			return;

		snapshot.hasBlood = true;
		snapshot.bloodFraction = blood.GetHealthScaled();

		snapshot.hasBleeding = true;
		snapshot.bleedRate = blood.GetTotalBleedingAmount();
	}

	//------------------------------------------------------------------------------------------------
	// Per-zone findings.
	//
	// Two passes rather than one, because the two questions have different answers. The DOT map says
	// which zones are actively bleeding and how fast; the hitzone list says which zones are damaged
	// at all. A tourniqueted limb is damaged and not bleeding; a fresh wound may be bleeding before
	// its zone reads much damage. A medic needs both, so a zone appearing in either is reported.
	//------------------------------------------------------------------------------------------------
	protected void ReadInjuries(notnull SCR_CharacterDamageManagerComponent damage, notnull ACM_VitalsSnapshot snapshot)
	{
		SCR_CharacterBloodHitZone blood = damage.GetBloodHitZone();

		array<HitZone> zones = {};
		damage.GetAllHitZones(zones);

		foreach (HitZone zone : zones)
		{
			if (!zone || zone == blood)
				continue;

			SCR_CharacterHitZone charZone = SCR_CharacterHitZone.Cast(zone);
			if (!charZone)
				continue;

			float dmg = 1.0 - charZone.GetHealthScaled();
			float bleed = BleedRateFor(blood, charZone);

			// Floating point noise on an untouched zone should not present as a wound.
			if (dmg < 0.01 && bleed <= 0)
				continue;

			snapshot.injuries.Insert(new ACM_VitalInjury(charZone.GetName(), dmg, bleed));
		}

		// Worst first. The details panel caps how many zones it can show, so whatever falls off the
		// end must be the least urgent. Ranked by bleed rate before raw damage: a wrecked-but-clotted
		// limb can wait, an intact-looking one that is pumping cannot.
		SortInjuries(snapshot.injuries);
	}

	//------------------------------------------------------------------------------------------------
	//------------------------------------------------------------------------------------------------
	protected void SortInjuries(notnull array<ref ACM_VitalInjury> injuries)
	{
		int count = injuries.Count();

		for (int i = 1; i < count; i++)
		{
			ref ACM_VitalInjury key = injuries[i];
			int j = i - 1;

			while (j >= 0 && InjuryRank(injuries[j]) < InjuryRank(key))
			{
				injuries[j + 1] = injuries[j];
				j--;
			}

			injuries[j + 1] = key;
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Bleeding dominates. A zone losing blood outranks a worse-damaged one that has stopped.
	protected float InjuryRank(ACM_VitalInjury injury)
	{
		if (!injury)
			return 0;

		return injury.bleedRate * 10.0 + injury.damage;
	}

	//------------------------------------------------------------------------------------------------
	protected float BleedRateFor(SCR_CharacterBloodHitZone blood, notnull SCR_CharacterHitZone zone)
	{
		if (!blood)
			return 0;

		map<SCR_CharacterHitZone, ref SCR_BleedingHitZoneParameters> dot = blood.GetHitZoneDOTMap();
		if (!dot)
			return 0;

		SCR_BleedingHitZoneParameters params = dot.Get(zone);
		if (!params)
			return 0;

		return params.m_fDamageOverTime;
	}

	//------------------------------------------------------------------------------------------------
	// INFERRED, AND SAID SO.
	//
	// Blood volume is the driver. A soldier at full volume sits at rest; as volume falls both rates
	// climb toward the ceilings above, which is the direction real haemorrhagic shock takes them.
	// Active bleeding adds on top, so someone freshly hit reads high before their volume has
	// actually dropped much - which is what makes the number useful for triage rather than just a
	// restatement of the blood bar.
	//
	// Unconsciousness overrides both downward. The dead get nothing: a readout showing a pulse on a
	// corpse is worse than a blank.
	//------------------------------------------------------------------------------------------------
	//------------------------------------------------------------------------------------------------
	// TRIAGE GRADE.
	//
	// Graded on the worst of three independent findings rather than an average, because a casualty is
	// as sick as their worst problem: a femoral bleed with full blood volume is still SEVERE, and
	// averaging it against eight untouched hitzones would call it MINOR.
	//
	//   blood volume   how much they have already lost
	//   bleed rate     how fast they are still losing it
	//   worst zone     whether any single body part is wrecked
	//
	// FATAL is reserved for alive-but-not-survivable: volume critical AND still haemorrhaging. That
	// pairing is the point - a slow trickle at low volume is SEVERE and treatable, while a fast bleed
	// on an empty tank is a person you will lose while you are still opening the pouch.
	//------------------------------------------------------------------------------------------------
	protected void GradeSeverity(notnull ACM_VitalsSnapshot snapshot)
	{
		if (snapshot.state == ACM_EVitalState.DEAD)
		{
			snapshot.severity = ACM_EInjurySeverity.FATAL;
			return;
		}

		float lost = 0;
		if (snapshot.hasBlood)
			lost = Math.Clamp(1.0 - snapshot.bloodFraction, 0, 1);

		float bleed = snapshot.bleedRate;

		float worstZone = 0;
		foreach (ACM_VitalInjury injury : snapshot.injuries)
		{
			if (injury && injury.damage > worstZone)
				worstZone = injury.damage;
		}

		if (lost <= 0.02 && bleed <= 0 && worstZone <= 0.02)
		{
			snapshot.severity = ACM_EInjurySeverity.NONE;
			return;
		}

		// Not survivable unattended.
		if (lost >= 0.75 && bleed > 0)
		{
			snapshot.severity = ACM_EInjurySeverity.FATAL;
			return;
		}

		// Any one of these on its own is enough.
		if (lost >= 0.55 || bleed >= 1.5 || worstZone >= 0.85 || snapshot.state == ACM_EVitalState.UNCONSCIOUS)
		{
			snapshot.severity = ACM_EInjurySeverity.SEVERE;
			return;
		}

		if (lost >= 0.25 || bleed > 0 || worstZone >= 0.4)
		{
			snapshot.severity = ACM_EInjurySeverity.MODERATE;
			return;
		}

		snapshot.severity = ACM_EInjurySeverity.MINOR;
	}

	//------------------------------------------------------------------------------------------------
	protected void DeriveCardiac(notnull ACM_VitalsSnapshot snapshot)
	{
		if (snapshot.state == ACM_EVitalState.DEAD)
		{
			snapshot.hasHeartRate = true;
			snapshot.hasRespiration = true;
			snapshot.derived = true;
			snapshot.heartRate = 0;
			snapshot.breathsPerMinute = 0;
			return;
		}

		if (!snapshot.hasBlood)
			return;

		snapshot.hasHeartRate = true;
		snapshot.hasRespiration = true;
		snapshot.derived = true;

		if (snapshot.state == ACM_EVitalState.UNCONSCIOUS)
		{
			snapshot.heartRate = HR_UNCONSCIOUS;
			snapshot.breathsPerMinute = RR_UNCONSCIOUS;
			return;
		}

		float lost = Math.Clamp(1.0 - snapshot.bloodFraction, 0, 1);
		float bleedPush = Math.Clamp(snapshot.bleedRate * 0.05, 0, 0.35);
		float stress = Math.Clamp(lost + bleedPush, 0, 1);

		snapshot.heartRate = Math.Round(HR_REST + (HR_MAX - HR_REST) * stress);
		snapshot.breathsPerMinute = Math.Round(RR_REST + (RR_MAX - RR_REST) * stress);
	}
}

//------------------------------------------------------------------------------------------------
// Selects the provider and answers every read.
//
// Selection is by priority and re-evaluated whenever a provider registers, so load order does not
// matter. Call sites never see which one won.
//------------------------------------------------------------------------------------------------
class ACM_VitalsService
{
	protected static ref ACM_VitalsService s_Instance;

	protected ref array<ref ACM_VitalsProvider> m_aProviders;
	protected ACM_VitalsProvider m_Active;

	//------------------------------------------------------------------------------------------------
	//! s_Instance is assigned BEFORE the vanilla provider is seeded, and the seeding goes through
	//! AddProvider rather than the static Register(). Registering from the constructor re-entered
	//! this method while s_Instance was still null - new had not returned yet - which recursed until
	//! the VM blew its stack. The rule is simply that nothing inside construction may resolve the
	//! singleton.
	static ACM_VitalsService GetInstance()
	{
		if (!s_Instance)
		{
			s_Instance = new ACM_VitalsService();
			s_Instance.AddProvider(new ACM_VanillaVitalsProvider());
		}

		return s_Instance;
	}

	//------------------------------------------------------------------------------------------------
	void ACM_VitalsService()
	{
		// Allocation only. See GetInstance for why nothing else may happen here.
		m_aProviders = new array<ref ACM_VitalsProvider>();
	}

	//------------------------------------------------------------------------------------------------
	//! Compatibility addons call this from their own init to take over the readout.
	static void Register(ACM_VitalsProvider provider)
	{
		if (!provider)
			return;

		GetInstance().AddProvider(provider);
	}

	//------------------------------------------------------------------------------------------------
	//! Instance-side registration. Safe to call during construction; Register() is the static door in.
	void AddProvider(ACM_VitalsProvider provider)
	{
		if (!provider)
			return;

		m_aProviders.Insert(provider);
		SelectProvider();
	}

	//------------------------------------------------------------------------------------------------
	protected void SelectProvider()
	{
		m_Active = null;

		foreach (ACM_VitalsProvider candidate : m_aProviders)
		{
			if (!candidate || !candidate.IsAvailable())
				continue;

			if (!m_Active || candidate.GetPriority() > m_Active.GetPriority())
				m_Active = candidate;
		}
	}

	//------------------------------------------------------------------------------------------------
	string GetActiveName()
	{
		if (!m_Active)
			return "none";

		return m_Active.GetName();
	}

	//! True when an advanced medical model is driving the readout.
	bool IsAdvanced()
	{
		return m_Active && m_Active.GetPriority() > 0;
	}

	//------------------------------------------------------------------------------------------------
	bool Read(IEntity character, notnull ACM_VitalsSnapshot snapshot)
	{
		// A provider can go unavailable after registering - a mod's own systems shutting down, for
		// instance - so availability is rechecked rather than trusted from registration time.
		if (!m_Active || !m_Active.IsAvailable())
			SelectProvider();

		if (!m_Active)
			return false;

		if (!m_Active.Read(character, snapshot))
			return false;

		snapshot.advanced = m_Active.GetPriority() > 0;
		return true;
	}
}
