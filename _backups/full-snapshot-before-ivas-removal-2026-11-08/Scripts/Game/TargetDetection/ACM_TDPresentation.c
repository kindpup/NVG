//------------------------------------------------------------------------------------------------
// ACM_TDPresentation.c
// Presentation-layer types: how much of what the device knows actually reaches the display.
//
// Kept out of ACM_TDTypes.c on purpose. That file describes the contact MODEL - what a contact is,
// where the position came from, how it ages. Nothing in here can change any of that. Every type
// below answers the same narrow question: given what the device already knows, what does the
// wearer see?
//
// The distinction matters because it is the one rule the whole clutter system rests on. A budget
// that could remove a contact from the store would make a busy screen into a claim about what the
// squad knows, and a receiver that quietly discarded shared contacts would make the data link lie.
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
// How loudly a hostile contact is presented.
//
// Cached on the contact and rewritten once per scan. The draw pass reads it and never computes it -
// ranking two dozen contacts sixty times a second to answer a question that changes ten times a
// second is work for nothing.
//
// Declared strongest first so a plain integer comparison orders them, which is what the sort and
// the budget walk both rely on.
//------------------------------------------------------------------------------------------------
enum ACM_TDContactPriority
{
	// Confirmed sight, close. The thing about to shoot you. Exempt from every budget.
	CRITICAL,
	// Confirmed sight at any range, or a frozen contact still at full confidence.
	HIGH,
	// Acquired and still reasonably fresh. The ordinary case.
	NORMAL,
	// Fading, or shared over the link and no longer young.
	LOW,
	// Confidence spent, or aged past the stale threshold. A mark that something was here.
	STALE
}

//------------------------------------------------------------------------------------------------
// Which classifications reach the display.
//
// Deliberately does NOT include friendlies - friendly IFF keeps its own independent mode, so
// switching to THREATS never turns your own squad off.
//------------------------------------------------------------------------------------------------
enum ACM_TDContactFilter
{
	// Everything the device resolved, including neutral third parties.
	ALL,
	// Hostiles and civilians, unknowns only while close enough to matter, no neutrals.
	RELEVANT,
	// Hostiles alone. Civilians still classify as civilians; they simply do not draw.
	THREATS
}

//------------------------------------------------------------------------------------------------
// How much a hostile marker says. Independent of the filter and of the budgets - this shifts what
// each tier is allowed to draw, it does not decide which contacts exist.
//------------------------------------------------------------------------------------------------
enum ACM_TDContactDetail
{
	// Markers only, at every tier. For players who want position and nothing else.
	MINIMAL,
	// The tier's normal presentation.
	STANDARD,
	// Adds age and reporting sensor to the top two tiers.
	DETAILED
}

//------------------------------------------------------------------------------------------------
// Discrete presets rather than sliders, because these have to be usable on a gamepad. A radial
// entry can cycle three values in one press; it cannot drag a number.
//------------------------------------------------------------------------------------------------
enum ACM_TDMarkerScalePreset
{
	SMALL,
	NORMAL,
	LARGE
}

enum ACM_TDFeedSizePreset
{
	SMALL,
	MEDIUM,
	LARGE
}

enum ACM_TDFeedPositionPreset
{
	TOP_LEFT,
	TOP_RIGHT,
	BOTTOM_LEFT,
	BOTTOM_RIGHT
}

//------------------------------------------------------------------------------------------------
// An observed non-combatant: a civilian, an unresolvable character, or a neutral third party.
//
// The third kind of thing this system tracks, and it is not a smaller contact. A contact is earned
// hostile intelligence with a lifecycle. A friendly is a person on your side whose position is
// known for free. A non-combatant is neither: the device has to actually see them, but seeing them
// buys no tactical state at all.
//
// So there is no dwell here, no confidence, no contact id, no network identity and no route to the
// data link. There is no method on this class that could give it any of those, which is the point -
// a civilian cannot drift into the hostile pipeline through a flag someone forgot to clear,
// because there is no field to set.
//
// ONE RULE GOVERNS THE POSITION. lastObservedPos is written only by RecordObservation, which is
// only called immediately after a successful raycast. The draw pass reads lastObservedPos and
// nothing else - never entity.GetOrigin(). The entity handle exists so the next trace has
// something to aim at, and for no other reason.
//
// That is not fussiness about a one-second window. Reading the live origin while a memory timer
// runs is a through-wall tracker; it is only a short one.
//------------------------------------------------------------------------------------------------
class ACM_TDNonCombatant
{
	// Aim point for the trace. NOT a position source for anything that draws.
	IEntity entity;

	ACM_TDAffiliation affiliation;

	// Range at the last confirmed observation, not a live range. Same honesty as a frozen
	// hostile's: it is how far away the device was when it learned what it knows. This is the one
	// that reaches the label.
	float distance;

	// Live range this scan, used only to sort and cap the list.
	//
	// Kept apart from the field above deliberately. Capping wants to know who is nearest NOW, and
	// the label must not - a readout that tracked an occluded civilian's real distance would be a
	// small position leak dressed up as a courtesy.
	float scanDistance;

	// The only position anything is allowed to draw from.
	vector lastObservedPos;

	// Seconds, from the same clock as everything else here. Negative until first observed.
	float lastVisibleTime;

	// Has a raycast ever confirmed this one? Until it has, the record exists but draws nothing.
	bool observed;

	// Has this record been traced at all this life? Drives the never-traced-first pass, so a
	// civilian who has just walked into view does not wait out a round robin.
	bool traced;

	// Has the wearer's event log already mentioned this one?
	//
	// Presentation bookkeeping and nothing else. Never cleared once set, so a non-combatant who
	// walks in and out of view all afternoon produces one line rather than forty. Nothing reads it
	// back into classification, position, ageing or the cap - deleting the field would change what
	// the log says and nothing else at all.
	bool announced;

	int packedColor;
	ref Color color;

	string labelText;
	float nextRefreshTime;

	//------------------------------------------------------------------------------------------------
	void ACM_TDNonCombatant(IEntity ent, ACM_TDAffiliation classification)
	{
		entity = ent;
		affiliation = classification;
		lastVisibleTime = -1;
		nextRefreshTime = -1;
		observed = false;
		traced = false;
		announced = false;
		distance = 0;
		scanDistance = 0;
		lastObservedPos = vector.Zero;
	}

	//------------------------------------------------------------------------------------------------
	// The single writer of lastObservedPos, called from exactly one place: straight after a clear
	// trace. Anything else that wanted to move the marker would have to add a second writer, which
	// is the change to refuse in review.
	//------------------------------------------------------------------------------------------------
	void RecordObservation(vector observedPos, float now, float range)
	{
		lastObservedPos = observedPos;
		lastVisibleTime = now;
		distance = range;
		observed = true;
	}

	//------------------------------------------------------------------------------------------------
	//! Seconds since the last confirmed observation. Negative if never observed.
	float AgeSeconds(float now)
	{
		if (lastVisibleTime < 0)
			return -1;

		return now - lastVisibleTime;
	}

	//------------------------------------------------------------------------------------------------
	// How much of the memory window is left, 1 down to 0. Drives marker alpha, so the marker fades
	// out where it stands instead of vanishing between frames - a snap reads as a dropped frame,
	// a fade reads as information going stale, which is what is actually happening.
	//------------------------------------------------------------------------------------------------
	float Freshness(float now, float memorySeconds)
	{
		if (!observed)
			return 0;

		if (memorySeconds <= 0)
			return 0;

		float age = AgeSeconds(now);
		if (age < 0)
			return 0;

		return Math.Clamp(1.0 - (age / memorySeconds), 0, 1);
	}
}
