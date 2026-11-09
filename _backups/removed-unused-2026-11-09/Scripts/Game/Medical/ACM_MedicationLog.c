// ACM Medication Log
//
// WHAT WAS GIVEN, AND WHEN.
//
// A casualty's current vitals say what is wrong now. They do not say what has already been done, and
// that is the difference between a medic making a decision and a medic guessing. Morphine two
// minutes ago and morphine twenty seconds ago are the same reading on the panel and completely
// different calls.
//
// Nothing in the engine records this. Damage effects can be enumerated, but they carry no applied-at
// time and vanish the moment they lapse - a bandage that has finished its work leaves no trace at
// all. So the timeline is observed here: the roster is sampled on the scan tick, the set of active
// treatment effects is compared against what was seen last time, and anything new is stamped.
//
// TYPE NAMES RATHER THAN TYPES, DELIBERATELY.
//
// Effects are classified by reading their class NAME as a string. That looks crude next to comparing
// typenames, and it is the entire reason this works with ACE.
//
// Referencing ACE_Medical_MorphineDamageEffect directly would be a compile-time dependency: the mod
// would not build without ACE installed. Asking the engine for every persistent effect and reading
// the names back means an advanced medical mod's own treatments are logged the day it is installed,
// with no bridge, no registration and no knowledge of its API. Anything it adds that reads as a
// treatment shows up; anything it renames stops showing up, which is the honest failure mode - a
// missing line rather than a wrong one.
//
// WHAT IS NOT CLAIMED.
//
// The stamp is when this client first SAW the effect, not when it was applied. A casualty treated
// out of view and found later reads as treated at the moment of discovery. That is a real limitation
// and it is why the panel labels the column with elapsed time rather than a clock time - "2:14 ago"
// invites the right amount of trust, where "14:32:06" would invite far too much.

//------------------------------------------------------------------------------------------------
//! One treatment, and when it was first observed.
class ACM_MedicationEntry
{
	string label;
	float observedAt;

	void ACM_MedicationEntry(string treatment, float when)
	{
		label = treatment;
		observedAt = when;
	}
}

//------------------------------------------------------------------------------------------------
//! Everything known about one casualty's treatment history.
class ACM_MedicationHistory
{
	IEntity entity;

	//! Type names seen on the last sample, so a treatment is only stamped when it first appears.
	ref array<string> active;

	ref array<ref ACM_MedicationEntry> entries;

	void ACM_MedicationHistory(IEntity owner)
	{
		entity = owner;
		active = new array<string>();
		entries = new array<ref ACM_MedicationEntry>();
	}
}

//------------------------------------------------------------------------------------------------
class ACM_MedicationLog
{
	protected static ref array<ref ACM_MedicationHistory> s_aHistories;

	//! Longest history kept per casualty. Older entries fall off the front.
	protected static const int MAX_ENTRIES = 8;

	//! Stop tracking a casualty nobody has looked at for this long, so the log cannot grow forever.
	protected static const float FORGET_SECONDS = 900.0;

	//------------------------------------------------------------------------------------------------
	// Fragments that mark an effect as a TREATMENT rather than an injury.
	//
	// Matched case-insensitively against the effect's class name, which is why an ACE morphine effect
	// is caught by the same rule as the vanilla one without either being named here.
	//------------------------------------------------------------------------------------------------
	protected static bool IsTreatment(string typeName, out string label)
	{
		string lower = typeName;
		lower.ToLower();

		if (lower.Contains("tourniquet"))
		{
			label = "TOURNIQUET";
			return true;
		}

		if (lower.Contains("bandage"))
		{
			label = "BANDAGE";
			return true;
		}

		if (lower.Contains("morphine"))
		{
			label = "MORPHINE";
			return true;
		}

		if (lower.Contains("saline"))
		{
			label = "SALINE";
			return true;
		}

		if (lower.Contains("epinephrine") || lower.Contains("adrenaline"))
		{
			label = "EPINEPHRINE";
			return true;
		}

		if (lower.Contains("bloodbag") || lower.Contains("transfusion"))
		{
			label = "BLOOD";
			return true;
		}

		// NO CATCH-ALL FOR "healing" OR "regen".
		//
		// There was one, and it was wrong. Every uninjured character carries passive regeneration
		// effects permanently, so the log stamped "+ TREATED" on people nobody had touched. A
		// medical panel inventing a treatment is far worse than one omitting an unrecognised drug:
		// a missing line makes a medic ask, a false line makes them assume.
		//
		// Anything new gets an explicit rule above, matched on its own name.

		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! Sample one casualty. Cheap enough for the scan tick; do not call it per frame.
	static void Observe(IEntity character)
	{
		if (!character)
			return;

		SCR_ExtendedDamageManagerComponent damage = SCR_ExtendedDamageManagerComponent.Cast(character.FindComponent(SCR_ExtendedDamageManagerComponent));
		if (!damage)
			return;

		if (!s_aHistories)
			s_aHistories = new array<ref ACM_MedicationHistory>();

		float now = System.GetTickCount() / 1000.0;

		ACM_MedicationHistory history = Resolve(character);
		if (!history)
			return;

		// EVERY persistent effect, then filtered by name. Asking for the base type with inherited
		// types included is what makes an unknown mod's treatments visible.
		array<ref SCR_PersistentDamageEffect> effects = damage.GetAllPersistentEffectsOfType(SCR_PersistentDamageEffect, true);

		array<string> seen = {};

		if (effects)
		{
			foreach (SCR_PersistentDamageEffect effect : effects)
			{
				if (!effect)
					continue;

				string typeName = effect.Type().ToString();

				string label;
				if (!IsTreatment(typeName, label))
					continue;

				seen.Insert(typeName);

				// Already running since the last sample - not a new administration.
				if (history.active.Find(typeName) != -1)
					continue;

				history.entries.Insert(new ACM_MedicationEntry(label, now));

				while (history.entries.Count() > MAX_ENTRIES)
					history.entries.Remove(0);
			}
		}

		history.active.Clear();
		foreach (string current : seen)
			history.active.Insert(current);
	}

	//------------------------------------------------------------------------------------------------
	//! Most recent first, formatted "MORPHINE  1:42". Returns how many lines were written.
	static int GetRecent(IEntity character, notnull out array<string> outLines, int maxLines)
	{
		outLines.Clear();

		if (!character || !s_aHistories)
			return 0;

		ACM_MedicationHistory history = Find(character);
		if (!history)
			return 0;

		float now = System.GetTickCount() / 1000.0;

		int written = 0;

		for (int i = history.entries.Count() - 1; i >= 0; i--)
		{
			if (written >= maxLines)
				break;

			ACM_MedicationEntry entry = history.entries[i];
			if (!entry)
				continue;

			string line = entry.label;
			line += "  ";
			line += Elapsed(now - entry.observedAt);

			outLines.Insert(line);
			written++;
		}

		return written;
	}

	//------------------------------------------------------------------------------------------------
	//! m:ss since administration. Elapsed rather than clock time - see the note at the top of the file.
	protected static string Elapsed(float seconds)
	{
		if (seconds < 0)
			seconds = 0;

		int total = Math.Round(seconds);
		int mins = total / 60;
		int secs = total % 60;

		// NOT named "out" - that is the parameter modifier keyword and using it here failed the
		// parse with a misleading "missing ';'" a few lines further down.
		string stamp = mins.ToString();
		stamp += ":";

		if (secs < 10)
			stamp += "0";

		stamp += secs.ToString();
		return stamp;
	}

	//------------------------------------------------------------------------------------------------
	protected static ACM_MedicationHistory Find(IEntity character)
	{
		if (!s_aHistories)
			return null;

		foreach (ACM_MedicationHistory history : s_aHistories)
		{
			if (history && history.entity == character)
				return history;
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	protected static ACM_MedicationHistory Resolve(IEntity character)
	{
		ACM_MedicationHistory history = Find(character);
		if (history)
			return history;

		history = new ACM_MedicationHistory(character);
		s_aHistories.Insert(history);

		Prune();
		return history;
	}

	//------------------------------------------------------------------------------------------------
	//! Drops histories whose entity is gone, and stale ones. Without this a long session accumulates
	//! a record for every casualty ever glanced at.
	protected static void Prune()
	{
		if (!s_aHistories)
			return;

		float now = System.GetTickCount() / 1000.0;

		for (int i = s_aHistories.Count() - 1; i >= 0; i--)
		{
			ACM_MedicationHistory history = s_aHistories[i];

			if (!history || !history.entity)
			{
				s_aHistories.Remove(i);
				continue;
			}

			if (history.entries.IsEmpty())
				continue;

			ACM_MedicationEntry newest = history.entries[history.entries.Count() - 1];
			if (newest && (now - newest.observedAt) > FORGET_SECONDS)
				s_aHistories.Remove(i);
		}
	}

	//------------------------------------------------------------------------------------------------
	static void Clear()
	{
		if (s_aHistories)
			s_aHistories.Clear();
	}
}
