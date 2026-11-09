//------------------------------------------------------------------------------------------------
// ACM_TDHudFeatures.c
// Small shared target-detection types: the rolling event log, and the bearing mark.
//
// The detection system fills them; any display reads them. (Moved here from the removed IVAS HUD.)
//
// Everything in this file is client-local and always will be. Nothing here is replicated, nothing
// here is written by the server, and nothing here may ever become an input to contact state. An
// event log entry is a note about something that already happened; if deleting the whole log could
// change what the device knows, the log would be part of the model and this file would be in the
// wrong place.
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
// What kind of thing happened. Decides the colour and the prefix, nothing else.
//------------------------------------------------------------------------------------------------
enum ACM_ETDEventKind
{
	// A hostile finished dwell and became a real contact.
	CONTACT,
	// Sight broke, or a contact aged out entirely.
	LOST,
	// Something arrived over the tactical data link.
	LINK,
	// A non-combatant was observed for the first time.
	CIVILIAN,
	// The device itself: mode changes, boot, master switch.
	SYSTEM
}

//------------------------------------------------------------------------------------------------
// One line in the log.
//------------------------------------------------------------------------------------------------
class ACM_TDEvent
{
	string text;
	ACM_ETDEventKind kind;

	// Seconds, from System.GetTickCount() like every other clock in this system. Drives the fade
	// and the prune, so it is written on push and on a same-line repeat and never otherwise.
	float time;
}

//------------------------------------------------------------------------------------------------
// The rolling tactical event log.
//
// Static and global on purpose. There is exactly one wearer per client and exactly one visor, so a
// per-instance log would mean handing a reference to every producer just so they could all write
// to the same place. The producers - acquisition, sight loss, expiry, link ingest, civilian
// sighting - are scattered through the scan pass and none of them holds a widget.
//
// Bounded hard at MAX_EVENTS. This is written from the scan pass, which runs several times a
// second in a firefight; an unbounded list would grow for as long as the round lasts and the visor
// would only ever read the last five entries of it.
//------------------------------------------------------------------------------------------------
class ACM_TDEventLog
{
	// How many lines the visor shows. The layout has exactly this many row widgets.
	static const int MAX_EVENTS = 5;

	// How long a line stays on screen. Long enough to read while moving, short enough that a quiet
	// minute clears the panel rather than leaving a stale wall of text implying live information.
	protected static const float HOLD_SECONDS = 14.0;

	// Below this the line is drawn faded rather than at full strength.
	protected static const float FADE_SECONDS = 4.0;

	// A repeat of the identical line inside this window refreshes the existing entry instead of
	// pushing a second copy. Without it a contact flickering at the edge of a trace budget writes
	// "SIGHT LOST" five times and pushes everything else off the panel.
	protected static const float REPEAT_WINDOW = 2.5;

	protected static ref array<ref ACM_TDEvent> s_aEvents = {};

	//------------------------------------------------------------------------------------------------
	// Newest first. The visor draws index 0 at the top.
	//------------------------------------------------------------------------------------------------
	static void Push(ACM_ETDEventKind kind, string text)
	{
		if (text.IsEmpty())
			return;

		float now = System.GetTickCount() / 1000.0;

		// Same line, just said. Refresh it rather than repeating it.
		if (!s_aEvents.IsEmpty())
		{
			ACM_TDEvent newest = s_aEvents[0];
			if (newest && newest.kind == kind && newest.text == text && (now - newest.time) < REPEAT_WINDOW)
			{
				newest.time = now;
				return;
			}
		}

		ACM_TDEvent entry = new ACM_TDEvent();
		entry.kind = kind;
		entry.text = text;
		entry.time = now;

		s_aEvents.InsertAt(entry, 0);

		while (s_aEvents.Count() > MAX_EVENTS)
			s_aEvents.Remove(s_aEvents.Count() - 1);
	}

	//------------------------------------------------------------------------------------------------
	// Borrowed, not handed over. The caller reads it and does not keep it.
	//------------------------------------------------------------------------------------------------
	static array<ref ACM_TDEvent> GetEvents()
	{
		return s_aEvents;
	}

	//------------------------------------------------------------------------------------------------
	// Drop anything past its hold window. Called from the visor refresh rather than from a timer of
	// its own - nothing needs pruning while nobody is looking at the panel.
	//------------------------------------------------------------------------------------------------
	static void Prune(float now)
	{
		for (int i = s_aEvents.Count() - 1; i >= 0; i--)
		{
			ACM_TDEvent entry = s_aEvents[i];
			if (!entry || (now - entry.time) > HOLD_SECONDS)
				s_aEvents.Remove(i);
		}
	}

	//------------------------------------------------------------------------------------------------
	// 1.0 while fresh, fading to 0 at the end of the hold window.
	//------------------------------------------------------------------------------------------------
	static float Visibility(ACM_TDEvent entry, float now)
	{
		if (!entry)
			return 0;

		float age = now - entry.time;
		if (age <= (HOLD_SECONDS - FADE_SECONDS))
			return 1.0;

		float remaining = HOLD_SECONDS - age;
		if (remaining <= 0)
			return 0;

		return Math.Clamp(remaining / FADE_SECONDS, 0, 1);
	}

	//------------------------------------------------------------------------------------------------
	// Cleared on device change and on shutdown. The log belongs to a session with one device; it
	// would be wrong for a fresh IVAS to boot showing the last one's contacts.
	//------------------------------------------------------------------------------------------------
	static void Clear()
	{
		s_aEvents.Clear();
	}

	//------------------------------------------------------------------------------------------------
	// Colour per kind, resolved rather than stored, because Color.FromRGBA cannot be a constant.
	//------------------------------------------------------------------------------------------------
	static Color ColorFor(ACM_ETDEventKind kind)
	{
		if (kind == ACM_ETDEventKind.CONTACT)
			return Color.FromRGBA(240, 120, 96, 255);

		if (kind == ACM_ETDEventKind.LOST)
			return Color.FromRGBA(190, 150, 110, 255);

		if (kind == ACM_ETDEventKind.LINK)
			return Color.FromRGBA(130, 190, 245, 255);

		if (kind == ACM_ETDEventKind.CIVILIAN)
			return Color.FromRGBA(225, 215, 175, 255);

		return Color.FromRGBA(140, 225, 190, 255);
	}
}

//------------------------------------------------------------------------------------------------
// One mark on the threat bearing ribbon.
//
// Carries a WORLD POSITION rather than a bearing, and that is the entire design of the ribbon.
// Bearing depends on where the wearer is standing, which changes every frame; the position the
// device knows about changes at walking pace. So the scan pass writes positions and the per-frame
// draw turns them into bearings, and a contact never lags behind the wearer's own movement.
//
// The position written here obeys exactly the same rule as the marker that draws it: live only
// while the contact's own sight test says live, and the frozen last-known position otherwise.
// There is no path from a mark back to a target entity, so the ribbon cannot track through a wall
// any more than the brackets can.
//------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------
// The glyph a ribbon mark draws.
//
// Deliberately the SAME shape language the markers already use on the main display, because the
// wearer has already learned it: a diamond is a friendly there and a diamond is a friendly here.
// Inventing a second vocabulary for the compass would mean two things to memorise for one system.
//------------------------------------------------------------------------------------------------
enum ACM_ETDMarkShape
{
	// Hostile contacts. Eight-sided, which at ribbon scale reads as a dot.
	DOT,
	// Friendlies. Matches AddRoleGlyphCommand on the main overlay.
	DIAMOND,
	// Non-combatants.
	SQUARE
}

class ACM_TDBearingMark
{
	vector worldPos;

	// Already faded by confidence where that applies. The ribbon does not re-tint.
	int packedColor;

	// Line width in reference units.
	float weight;

	// Fraction of the ribbon's mark height, so a stale contact reads as a stub and a critical one
	// reaches the top of the band.
	float height;

	// Drawn broken rather than solid. The one visual difference between "I can see this" and
	// "this is where it was", carried onto the ribbon so the compass never implies live tracking
	// the markers themselves are refusing to claim.
	bool frozen;

	ACM_ETDMarkShape shape;

	// Radius of the glyph in reference units.
	float glyphRadius;

	// Solid, or an outline.
	//
	// FILLED MEANS LIVE SIGHT AND NOTHING ELSE. It is the strongest thing the ribbon can say, so it
	// is reserved for the one case where the wearer's own sensor is looking at the target right now.
	// Everything remembered, and everything anybody else reported, is hollow.
	bool filled;

	// Somebody else's report, arrived over the tactical data link.
	//
	// Drawn as a second ring inside the first. There is no filled-and-link case and there never can
	// be: HasLiveSight() requires delivery == LOCAL, so a link contact is structurally incapable of
	// being live on this client. The double ring is not a style choice sitting on top of that rule -
	// it is what that rule looks like.
	bool link;
}
