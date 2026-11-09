//------------------------------------------------------------------------------------------------
// ACM_TDTypes.c
// Shared enums and small value types for the ACM NVG target-detection / character
// highlighting system.
//
// This system is opt-in per device: an NVG entity only gains target detection if it
// carries an ACM_TargetDetectionComponent alongside its ACM_NightVisionComponent.
// Everything here is client-side and local-player only - nothing is replicated yet.
//
// A contact is deliberately modelled as INFORMATION rather than as a thing the wearer's own
// goggles are looking at. It carries where the target was last observed, when, how confident
// the system is about that, and WHICH SENSOR reported it. Nothing in the model assumes the
// reporting sensor was the local player's NVG, because in time it will not be: a UAV, and
// later another wearer over the data link, will file contacts into this same structure.
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
// HOW THIS CLIENT CAME TO KNOW ABOUT THE CONTACT.
//
// Delivery is RECEIVER-LOCAL. The same target is LOCAL on the client whose own sensor is looking
// at it and DATALINK on every client that was told about it, at the same instant. It is not a
// property of the target and it is never authoritative anywhere but on the machine holding it.
//
// This is the axis that gates live tracking, so it is the one that must never be shared. A server
// that "promoted" a contact to LOCAL because some reporter had eyes on would be telling forty
// clients they personally have sight of something none of them can see.
//------------------------------------------------------------------------------------------------
enum ACM_TDDelivery
{
	// This client's own sensor produced it. The only value that can lead to live tracking.
	LOCAL,
	// Arrived over the tactical data link. Stored position only, always, whatever the originating
	// sensor was and however fresh the report is.
	DATALINK
}

//------------------------------------------------------------------------------------------------
// WHAT KIND OF SENSOR ORIGINALLY OBSERVED THE TARGET.
//
// Sensor source travels with a contact across the link, because it describes the observation
// rather than the observer's relationship to it. A hostile spotted by a soldier and shared to the
// platoon is VISUAL on every machine that hears about it - what changes between machines is
// DELIVERY, not this.
//
// Deliberately carries no authority. Nothing may infer sight, freshness or trust from a sensor
// value alone; those come from delivery, confidence and age.
//------------------------------------------------------------------------------------------------
enum ACM_TDSensor
{
	// Mark one eyeball, unaided or through optics. What the local scan produces.
	VISUAL,
	// Drone or UAV sensor.
	UAV,
	// Reserved. No producer yet; present so a thermal sensor is a new reporter rather than a new
	// system.
	THERMAL,
	// Reserved. Laser designation.
	LASER,
	// Reserved. A human placing a mark by hand, which is the one source with no sensor behind it
	// at all and therefore the one that will need its own confidence rules.
	MANUAL_MARK
}

//------------------------------------------------------------------------------------------------
// IFF bucket a detected character falls into.
//
// Five states, and the boring ones carry as much weight as HOSTILE does. UNKNOWN means the device
// could not resolve a faction - a character half streamed in, usually - and it stays UNKNOWN. There
// is no heuristic anywhere that promotes it to hostile, because guessing wrong in that direction is
// how a system like this gets somebody shot.
//
// CIVILIAN is resolved from an authored faction key list rather than inferred. The obvious inferred
// test, IsPlayable(), was tried and does not mean what it looks like it means: it reports whether a
// faction is selectable by players in the current scenario, so in a Game Master sandbox it reads
// false for every side including the wearer's own.
//
// NEUTRAL keeps its original meaning - a faction that resolved cleanly and is declared neither
// friendly nor hostile, which in practice is a third military side.
//
// CIVILIAN appended rather than inserted so no authored enum index shifts under an existing prefab.
//------------------------------------------------------------------------------------------------
enum ACM_TDAffiliation
{
	UNKNOWN,
	FRIENDLY,
	HOSTILE,
	NEUTRAL,
	CIVILIAN
}

//------------------------------------------------------------------------------------------------
// Shape of the device's usable display region.
//
// A marker is only meaningful where the wearer can actually see it. On a tube device that is a
// circle; on a flat IVAS-style visor it is a rectangle. Anything projected outside the region is
// not drawn at all - not clipped by a widget, but skipped before it becomes a draw command, so an
// off-display contact costs nothing.
//------------------------------------------------------------------------------------------------
enum ACM_TDDisplayMask
{
	// Draw across the whole screen. What the system did before masking existed.
	NONE,
	// Tube optics: a circle, sized off screen HEIGHT on both axes so it stays round on a
	// wide monitor rather than stretching into an ellipse.
	CIRCLE,
	// Flat visor: an axis-aligned rectangle with independent half-extents.
	RECTANGLE
}

//------------------------------------------------------------------------------------------------
// Which friendlies receive an IFF marker.
//
// This decides VISIBILITY only. Once a friendly passes the mode test, their tactical role decides
// how they are presented - the two are sequential, not alternatives.
//------------------------------------------------------------------------------------------------
enum ACM_TDFriendlyIffMode
{
	// No friendly markers at all.
	OFF,
	// Only the wearer's own group leader. The minimum useful case: know where your leader is.
	GROUP_LEADER,
	// Everyone in the wearer's own group.
	GROUP,
	// Every friendly the sensor can find, group or not.
	ALL_FRIENDLIES
}

//------------------------------------------------------------------------------------------------
// How much a friendly marker says.
//
// Density matters more than it sounds: a squad of twelve at STANDARD is readable, and the same
// twelve at DETAILED is a wall of text over the middle of the display.
//------------------------------------------------------------------------------------------------
enum ACM_TDIffDetail
{
	// Glyph and role abbreviation.            <>  MED
	MINIMAL,
	// Glyph, name, role and range.            <>  SMITH  MED - 42m
	STANDARD,
	// Adds the group.                         <>  SMITH  MEDIC - 42m  ALPHA 1-2
	DETAILED
}

//------------------------------------------------------------------------------------------------
// A friendly eligible for an IFF marker.
//
// Deliberately NOT an ACM_TDContact, and the difference is the point. A contact is earned hostile
// intelligence with a lifecycle - dwell, line of sight, a frozen position, decaying confidence. A
// friendly is none of those things: they are simply there, their position is known because they
// are on your side, and drawing them costs one projection.
//
// So this type carries no LOS state, no confidence, no acquisition and no last-known position. It
// has no source field either - source describes where HOSTILE intelligence came from and has no
// meaning here.
//
// Everything slow-changing is cached: the label strings are rebuilt on the IFF refresh interval
// rather than per frame, because a name, a role and a group change on the order of never while
// the draw pass runs sixty times a second.
//------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------
// What a squadmate is DOING, for the visor's status strip.
//
// Every value here is read straight off the character's own controller or its compartment access -
// state the engine already replicates so the animation system can draw them correctly. Nothing is
// inferred from AI internals, nothing is asked over the network, and nothing is guessed.
//
// ENGAGING is the one that needs stating plainly: it means WEAPON RAISED, which is the honest
// observable. It is not "this player is in a firefight" - there is no such flag to read, and a
// readout claiming it would be a guess dressed as intelligence. Weapon up is what the wearer would
// see if they looked over, and it is what the strip says.
//
// Declared worst-first so a plain integer comparison orders them by how much the wearer needs to
// know, which is what the row colouring reads.
//------------------------------------------------------------------------------------------------
enum ACM_ETacticalStatus
{
	// Controller reports dead.
	KIA,
	// Incapacitated or unconscious. Still alive, needs somebody now.
	DOWN,
	// Weapon raised.
	ENGAGING,
	// In a vehicle seat.
	MOUNTED,
	// Moving fast enough that it is a run rather than a walk.
	SPRINTING,
	// Moving.
	MOVING,
	// Stationary and weapon down.
	HOLDING,
	// No controller to ask - streamed out, or something that is not a character.
	UNKNOWN
}

class ACM_TDFriendly
{
	IEntity entity;
	int playerId;

	ACM_ETacticalRole role;
	int packedColor;
	ref Color color;

	// Draw ordering. Taken from the role preset, so leadership and specialists land on top when
	// markers overlap and survive the cap when more friendlies are visible than can be drawn.
	int priority;

	float distance;

	// Rebuilt on the refresh interval, not per frame.
	string labelText;
	float nextRefreshTime;

	//------------------------------------------------------------------------------------------------
	// Status strip state. Resolved on the same refresh tick as the label, for the same reason: a
	// squadmate's stance and speed change on the order of once a second, and asking their controller
	// sixty times a second per squadmate to redraw the same word would dwarf every other cost here.
	//------------------------------------------------------------------------------------------------
	ACM_ETacticalStatus status;

	//------------------------------------------------------------------------------------------------
	// ROSTER BLOCK.
	//
	// True for actual group members, who are listed unconditionally and pinned to the top of the
	// strip. False for a friendly who is only on the strip because the wearer can currently SEE
	// them - those come below, and drop off the moment they leave view.
	//
	// The distinction is the whole point of the two-block layout: a squadmate behind a wall is still
	// your squadmate and still needs a status line; a stranger you glanced at is not.
	bool inSquad;

	//------------------------------------------------------------------------------------------------
	// CASUALTY STATE, resampled on the status tick rather than per frame.
	//
	// Held by value on the record rather than read at draw time, because the readout is rebuilt every
	// frame and walking a damage manager per person per frame to render the same word is exactly the
	// cost the refresh interval exists to avoid.
	ref ACM_VitalsSnapshot vitals;

	// Where they were at the last status sample, and when.
	//
	// Speed is measured from these rather than read from a physics component, because the sample
	// interval is already there and a position delta works on any character whether or not it has an
	// active physics body. Negative time means never sampled - the first sample establishes the
	// baseline and reports no speed at all, rather than reporting the distance from the origin.
	vector lastStatusPos;
	float lastStatusTime;

	// Metres per second at the last sample.
	float speed;

	//------------------------------------------------------------------------------------------------
	void ACM_TDFriendly(IEntity ent, int player)
	{
		entity = ent;
		playerId = player;
		role = ACM_ETacticalRole.UNASSIGNED;
		nextRefreshTime = -1;

		status = ACM_ETacticalStatus.UNKNOWN;
		lastStatusTime = -1;
		lastStatusPos = vector.Zero;
		speed = 0;
	}
}

//------------------------------------------------------------------------------------------------
// How candidate characters are gathered each scan.
//------------------------------------------------------------------------------------------------
enum ACM_TDScanMode
{
	// Broadphase sphere query around the viewer. Scales with detection range rather
	// than with total world population, so this is the right default.
	SPHERE_QUERY,
	// Walk every AI agent in the world plus the player list. Proven approach (it is
	// what the Shadow Admin ESP uses) but O(all agents). Kept as an escape hatch if
	// the sphere query ever misses characters on a given build.
	AGENT_SCAN
}

//------------------------------------------------------------------------------------------------
// A resolved, drawable contact. Built on the slow scan pass so the per-frame draw only
// has to re-read bone positions and re-project them - colours, label text and skeleton
// lookups are all resolved once, here.
//------------------------------------------------------------------------------------------------
class ACM_TDContact
{
	// May be null, and NOT the contact's identity. A contact reported by a drone or over the
	// data link can concern a target that is not streamed in on this client at all, in which
	// case there is no entity to hold - the contact is still perfectly valid, it just draws
	// from stored data. Identity lives in targetId below.
	IEntity entity;

	// Cross-machine identity, from Replication.FindItemId(). This is the dedup key: when a second
	// sensor reports a target that is already being tracked, the existing contact is refreshed
	// rather than a duplicate being filed.
	//
	// NOT A SIGNED COUNTER. An RplId carries its high bit set, so read as an int it is a large
	// NEGATIVE number - a real target id looks like -2147481878. Testing it with "< 0" therefore
	// rejects every replicated entity in the game, which is exactly the bug that made the data link
	// silently discard every report it was ever offered. Always compare against
	// Replication.INVALID_ID; never against zero.
	int targetId;

	// How THIS client got the contact. Never transmitted - each machine sets its own.
	ACM_TDDelivery delivery;

	// What kind of sensor made the original observation. Travels across the link intact.
	ACM_TDSensor sensor;

	// Server-issued opaque network identity, or -1 for a purely local contact that has never been
	// reported. This is what receivers dedup on and the ONLY id that crosses the wire - the
	// replicated targetId below stays on the machine that resolved it, because
	// Replication.FindItem() would turn it straight back into a live entity handle.
	int contactId;

	ACM_TDAffiliation affiliation;
	int packedColor;                      // ARGB, already packed for the canvas
	string labelText;
	ref Color labelColor;
	float distance;
	float lastSeenTime;                   // seconds; time of the last confirmed LOS
	bool hasLineOfSight;
	bool traced;                          // has this contact ever been LOS-tested?

	//------------------------------------------------------------------------------------------------
	// IVAS contact state (Phase 1)
	//
	// Only populated for contacts running the IVAS lifecycle - hostiles, when the device has
	// it switched on. Everything else keeps the original memory-window behaviour untouched.
	//------------------------------------------------------------------------------------------------

	// True when this contact follows the IVAS rules rather than the legacy memory window.
	bool ivas;

	// THE frozen position. Once line of sight breaks this is the only thing the draw pass is
	// allowed to read - the entity handle is not touched again until sight is regained. This
	// single rule is what makes wall tracking structurally impossible rather than merely
	// switched off: there is no code path from the marker back to the target's live
	// transform.
	vector lastKnownPos;

	// Did the most recent trace confirm sight? Distinct from hasLineOfSight, which the legacy
	// path also writes - this one gates the live draw and the entity outline.
	bool locallyConfirmed;

	// When the current unbroken run of sight began, or -1 while not accumulating. Reset on
	// every break, so dwell has to be earned again after a target ducks behind cover.
	float dwellStart;

	// Has dwell ever been satisfied? A contact that has not earned acquisition draws nothing
	// at all - it is a candidate, not a contact.
	bool acquired;

	// When acquisition first completed. Kept for Phase 4 and for age readouts.
	float firstSeenTime;

	// 1.0 while sight holds, decaying once it breaks. Everything visual reads from this, so
	// there is one number to tune rather than thresholds scattered through the draw pass.
	float confidence;

	//------------------------------------------------------------------------------------------------
	// Presentation state. Written once per scan, read by the draw pass, and read by nothing else.
	//
	// None of these three may influence dedup, ageing, expiry or the data link. A contact whose
	// marker was squeezed off a crowded display is in every other respect exactly the contact it
	// was a frame earlier.
	//------------------------------------------------------------------------------------------------

	// What the contact has earned: sight, confidence, age, how it was delivered.
	ACM_TDContactPriority priority;

	// What the display can afford, after the budgets have had their say. Equal to priority until
	// something has to give.
	ACM_TDContactPriority drawTier;

	// Cleared by a budget or by the contact filter. Never by anything that owns contact state.
	bool drawVisible;

	//------------------------------------------------------------------------------------------------
	// Does the wearer's own sensor have sight of this contact RIGHT NOW?
	//
	// The renderer's single most important question, because it is what decides whether the
	// live entity may be read and whether a through-wall outline is permitted. The two contact
	// lifecycles answer it differently: an IVAS contact has to have earned acquisition and be
	// currently confirmed, while a legacy contact just goes on the raw trace result.
	//
	// Anything that is not live sight - a remembered contact, an expired one, and in due course
	// a contact shared over the data link - answers false and is drawn from stored data only.
	//------------------------------------------------------------------------------------------------
	// The only correct way to ask whether this contact has a usable network identity.
	bool HasNetworkIdentity()
	{
		return targetId != Replication.INVALID_ID;
	}

	//------------------------------------------------------------------------------------------------
	bool HasLiveSight()
	{
		// The hard gate, and the reason this is a method rather than a field.
		//
		// BOTH axes have to agree. Delivery must be LOCAL - this client's own sensor produced it,
		// not the network - and the sensor must be VISUAL, because that is the only kind this
		// client can raycast against. A drone contact, a data-link contact, and a hypothetical
		// local thermal return all answer false here NO MATTER WHAT OTHER STATE THEY CARRY.
		//
		// Defense in depth, kept deliberately even though the network path now also refuses to
		// deliver anything as LOCAL. A future sensor that filled in locallyConfirmed by mistake
		// still could not produce wall tracking, because the renderer and the outliner both ask
		// this question and it still says no.
		if (delivery != ACM_TDDelivery.LOCAL)
			return false;

		if (sensor != ACM_TDSensor.VISUAL)
			return false;

		if (!entity)
			return false;

		if (ivas)
			return locallyConfirmed;

		return hasLineOfSight;
	}

	//------------------------------------------------------------------------------------------------
	// A sensor confirming where this target is. THE single writer of lastKnownPos, for every
	// sensor there will ever be.
	//
	// It lives on the contact rather than in the visual scanner precisely so that a drone sensor
	// cannot invent its own freezing rules: whatever files the sighting, the position it reports
	// is stored the same way, ages the same way and decays the same way. A sensor that stops
	// seeing its target simply stops calling this, and the stored position stands untouched
	// until the contact expires.
	//
	// Refresh and dedup are the same operation. A target already tracked from one sensor and
	// then picked up by another lands here on the existing contact, so the most recent
	// confirmation wins and no duplicate is created.
	//------------------------------------------------------------------------------------------------
	void RecordSighting(vector observedPos, float now, ACM_TDDelivery how, ACM_TDSensor by)
	{
		lastKnownPos = observedPos;
		lastSeenTime = now;
		delivery = how;
		sensor = by;
	}

	//------------------------------------------------------------------------------------------------
	// A sensor losing its target. Deliberately does NOT write lastKnownPos - the stored value is
	// the last position actually observed and has to survive untouched.
	//
	// Confidence decay is not applied here because it needs the device's tuning values; the
	// system's BreakSight() applies it immediately after calling this.
	//------------------------------------------------------------------------------------------------
	void RecordSightingLost()
	{
		locallyConfirmed = false;

		// Dwell restarts from scratch, so a target flickering in and out of a sensor's view
		// cannot accumulate acquisition one frame at a time.
		dwellStart = -1;
	}

	//------------------------------------------------------------------------------------------------
	void ACM_TDContact(IEntity ent)
	{
		entity = ent;

		// Deliberately starts hidden and untraced. Line-of-sight checks are budgeted across
		// scans, so a brand new contact may wait a tick or two for its first raycast - and
		// assuming it visible until then would draw every target through walls for exactly
		// as long as the backlog lasts.
		hasLineOfSight = false;
		traced = false;

		targetId = Replication.INVALID_ID;

		// Born local and visual: a contact only comes into existence here through the local scan.
		// Anything arriving over the link is constructed by the ingest path, which sets DATALINK
		// before the contact is ever drawn.
		delivery = ACM_TDDelivery.LOCAL;
		sensor = ACM_TDSensor.VISUAL;
		contactId = -1;

		ivas = false;

		// Born invisible at the lowest tier. A contact that has not been through a scan has no
		// earned presentation, and defaulting it to CRITICAL would flash every new candidate.
		priority = ACM_TDContactPriority.STALE;
		drawTier = ACM_TDContactPriority.STALE;
		drawVisible = false;

		locallyConfirmed = false;
		acquired = false;
		dwellStart = -1;
		firstSeenTime = -1;
		confidence = 0;
		lastKnownPos = vector.Zero;
	}

	//------------------------------------------------------------------------------------------------
	//! Seconds since sight was last confirmed. Negative until the contact has ever been seen.
	float AgeSeconds(float now)
	{
		if (lastSeenTime <= 0)
			return -1;

		return now - lastSeenTime;
	}
}
