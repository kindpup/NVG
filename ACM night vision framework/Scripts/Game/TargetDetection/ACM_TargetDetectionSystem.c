//------------------------------------------------------------------------------------------------
// ACM_TargetDetectionSystem.c
// Client-side singleton that owns the scan pass, the canvas overlay and the draw loop for
// NVG target detection.
//
// Split of work, and why:
//   - Scan pass (slow timer, default 10Hz): everything expensive and everything that does
//     not need to be frame-accurate. Gathering candidates, range/FOV culling, line-of-sight
//     raycasts, faction resolution, label strings.
//   - Draw pass (every frame): re-projects each surviving contact and rebuilds its box. This
//     is what makes the boxes glue to moving characters instead of stepping at the scan rate.
//
// Contacts are drawn as projected body boxes, matching the Shadow Admin ESP's visual
// language. There is no skeletal tracking in this system: no rig walk, no bone reads, no
// hull building. A box is two projected points and four lines.
//
// The draw pass runs on a callqueue timer with period 0 rather than from a modded
// SCR_PlayerController.OnUpdate, for two reasons. OnUpdate stops ticking while a menu is
// open - and a CanvasWidget keeps rendering its last submitted command list, so the
// outlines would freeze on screen over the open inventory instead of clearing. And Shadow
// Admin already mods SCR_PlayerController; not declaring it here means there is no
// load-order-dependent chain to get wrong when both mods are installed.
//
// Only one device draws at a time. If a player somehow has two detection-capable devices
// lit, the most recently activated one wins rather than both stacking their draw work.
//
//------------------------------------------------------------------------------------------------
// INTENDED SEPARATION OF RESPONSIBILITIES
//
// This class currently does three jobs that are meant to end up in three places:
//
//   sensor            produces information - "I saw a hostile here, at this time"
//   contact manager   OWNS contacts - dedup, ageing, expiry, confidence
//   HUD               displays contacts
//
// and a fourth, entirely separate from all of them:
//
//   drone feed        displays VIDEO, and knows nothing about contacts
//
// The tactical-contact system handles information. The drone-feed component handles video.
// Those two must not be mixed: a PiP window is a camera and a render target, not a contact,
// and it belongs in its own component with its own lifetime so that hiding it stops the
// rendering outright rather than merely hiding a widget.
//
// Nothing in this file assumes the local NVG is the only sensor any more. A contact carries the
// delivery and sensor axes that describe it (ACM_TDDelivery / ACM_TDSensor), sensors file
// sightings through the contact's
// own RecordSighting/RecordSightingLost rather than writing its state directly, contacts are
// deduplicated on a replicated target id rather than on an entity pointer, and the renderer
// draws from stored data for anything the wearer cannot personally see. A UAV sensor can
// therefore be added as a second producer without this file changing, and a data-link receiver
// as a third.
//
// What has NOT been done, deliberately: the store still physically lives here rather than in a
// standalone contact manager. That split is the right first move of Phase 2, when there is a
// second producer to justify it - not a speculative refactor now.
//------------------------------------------------------------------------------------------------

class ACM_TargetDetectionSystem
{
	protected static ref ACM_TargetDetectionSystem s_Instance;

	// Must match the number of ACM_TDLabel* widgets authored in the overlay layout.
	// Shared by the hostile and friendly passes. Doubled from 24 when friendly IFF landed: with
	// both passes drawing from one pool, a full squad of friendlies plus a contested contact list
	// would otherwise exhaust it and silently drop labels.
	protected static const int LABEL_POOL_SIZE = 48;

	// Path only, deliberately, and this is the second attempt at it.
	//
	// NOW QUALIFIED - and this is the fix for "the IVAS HUD never comes back after a menu".
	//
	// History: the layout shipped with no .meta, so the resource database had no id for it and the
	// engine logged "Wrong GUID for resource {0000000000000000}...". A .meta was authored with a
	// fresh id, but an id is only valid once Workbench has indexed that .meta - so until the addon
	// had been opened in Workbench, the qualified form was a hard error and this had to stay
	// path-only. That condition is long since satisfied; the reference simply never got updated.
	//
	// The cost of leaving it path-only was not the cosmetic startup warning it looked like. A
	// path-only reference resolves to a null id, and rebuilding the widget from it after the menu
	// system has torn down and rebuilt the workspace layers produces a widget that reports itself
	// visible, parented and canvas-bearing while rendering nothing. Everything upstream stayed
	// healthy - the scan kept finding targets, the draw pass kept producing commands onto a live
	// canvas - which is exactly why this hid behind three plausible-looking wrong diagnoses.
	//
	// If this ever has to be moved to a project that has not been opened in Workbench, revert to
	// the path-only form rather than shipping a broken id - and expect this bug back with it.
	protected static const ResourceName OVERLAY_LAYOUT = "{EF46004FF3806EF6}UI/layouts/HUD/ACM_TargetDetectionOverlay.layout";

	// Fallback for the case above: if the qualified id ever fails to resolve, the path-only form is
	// tried once before giving up, so a resource-database problem degrades to the old behaviour
	// instead of a HUD that never appears at all.
	protected static const ResourceName OVERLAY_LAYOUT_FALLBACK = "UI/layouts/HUD/ACM_TargetDetectionOverlay.layout";

	// Assumed standing height, in metres, used to build the box. Fixed rather than measured:
	// the box is a contact marker, not a tracing of the target's pose, and a fixed height
	// keeps its proportions identical at every range.
	//
	// Everything else about marker geometry - aspect, scale, minimum size, bracket arm length -
	// is a device attribute rather than a constant, so a given pair of goggles can be tuned
	// without a rebuild.
	protected static const float BOX_BODY_HEIGHT_M = 1.85;

	// Centre mass, not the origin. A trace to the feet is blocked by every low wall and hedge the
	// target happens to be standing behind. Shared so the feed's sensor cannot drift from the
	// wearer's - a looser trace on one of them is a wallhack on that one only.
	protected static const float TRACE_AIM_HEIGHT_M = 1.1;

	//! Half-angle of the look-at cone, degrees, measured to the EDGE of the body rather than its
	//! centre - see LOOKAT_BODY_RADIUS_M. Tight enough that only one casualty qualifies.
	protected static const float LOOKAT_CONE_DEG = 5.0;

	//! Assumed body half-width, metres, used to widen the cone by the target's own angular size.
	//!
	//! A fixed angular cone is the wrong shape for this. Five degrees at twenty metres is a
	//! reasonable "you are pointing at him"; five degrees at two metres is a seventeen-centimetre
	//! circle, and standing over a casualty you are almost never within seventeen centimetres of
	//! his ORIGIN even with the crosshair square on his chest. Subtracting the body's angular
	//! radius makes the test "is the crosshair on him", which is the question a wearer thinks they
	//! are asking, at every distance.
	protected static const float LOOKAT_BODY_RADIUS_M = 0.45;

	//! Beyond this the panel does not raise, measured from the WEARER rather than his camera.
	//!
	//! Seven metres, not twenty-five. This is a hands-on readout: blood volume, bleed rate, which
	//! limb is open. That is what you read while kneeling next to a man, and reading it off someone
	//! across a field is not triage, it is a scanner. Keeping it to arm's reach also keeps the panel
	//! quiet - at range half the bodies on a battlefield are inside a five-degree cone at some point.
	protected static const float LOOKAT_RANGE_M = 7.0;

	//! Query radius padding, metres. The sphere is centred on the CAMERA and the range is measured
	//! from the wearer, and in third person those are metres apart.
	protected static const float LOOKAT_QUERY_PAD_M = 6.0;

	//! Height above the casualty's origin the callout anchors to. Roughly head height on a prone body.
	protected static const float LOOKAT_ANCHOR_HEIGHT_M = 0.6;

	//! How long the panel stays up after the ray stops touching them.
	//!
	//! Without this the panel is a strobe: it is a single ray re-tested from scratch, so it dropped
	//! the instant the crosshair slipped off a limb - and looking at a prone casualty while walking
	//! means slipping off constantly. Holding briefly turns "am I pointing at him right now" into
	//! "am I attending to him", which is the question actually being asked.
	protected static const float LOOKAT_HOLD_SECONDS = 1.75;

	//! TEMPORARY. Traces every gate in the look-at selection.
	protected static const bool ACM_LOOKAT_DEBUG = true;

	//! Hide the casualty panel while the weapon is up.
	//!
	//! OFF, because IsWeaponRaised() is not the signal it looks like. It reported TRUE with the
	//! wearer holding nothing at all - the HUD read NO WEAPON while this gate suppressed the panel
	//! on every frame - so it is not a usable proxy for "in combat", and gating a medical readout
	//! on it made the feature dead on arrival.
	//!
	//! Kept as a switch rather than deleted: hiding the panel mid-firefight is still the right
	//! idea, it just needs a signal that means what it says. Recent-fire time or an aiming state
	//! would both serve; neither is wired up yet.
	protected bool m_bLookAtSuppressWeaponRaised = false;
	protected float m_fNextLookAtDiag;

	//! Last look-at diagnostic emitted, so an unchanging state is not reprinted every second.
	protected string m_sLastLookAtDiag;

	//! Scratch for the look-at sphere query. Separate from m_aQueryResults on purpose: both run on
	//! the same tick and sharing one buffer would have each clobbering the other's results.
	protected ref array<IEntity> m_aLookAtQuery = {};

	// Camera-space depth below which a point is behind the near plane.
	protected static const float NEAR_DEPTH = 0.1;


	// Acquisition / freeze / expiry / classification tracing. Off for normal play - the
	// classify pass costs a string per candidate per second when it is on.
	// Off. Every trace in this file is behind it, including the friendly-IFF tracing added while
	// chasing why that path drew nothing - that tracing is left in place rather than deleted,
	// because it found a bug that three rounds of reading the code did not, and flipping this back
	// to true is cheaper than writing it again.
	protected static const bool ACM_TD_DEBUG = false;

	protected float m_fNextClassifyLogTime;
	protected float m_fNextFriendlyDrawLogTime;

	//------------------------------------------------------------------------------------------------
	// Active providers. Normally zero or one; the array exists so that a second device
	// powering on and off cannot leave the first one's state behind.
	//------------------------------------------------------------------------------------------------
	protected ref array<ACM_TargetDetectionComponent> m_aProviders = {};
	protected ACM_TargetDetectionComponent m_Provider;

	protected ref array<ref ACM_TDContact> m_aContacts = {};

	// Friendlies are kept entirely separately from contacts, because they are a different kind of
	// thing. A contact is earned hostile intelligence with a lifecycle; a friendly is a person on
	// your side whose position is simply known. Sharing a store would have meant sharing the
	// lifecycle, and the first thing that would have leaked back in is a line-of-sight raycast per
	// squadmate per scan - the exact cost this path exists to avoid.
	protected ref array<ref ACM_TDFriendly> m_aFriendlies = {};

	// This scan's friendlies, before sorting and capping. Kept as a member rather than a local so
	// it is cleared and refilled instead of reallocated every scan.
	protected ref array<ref ACM_TDFriendly> m_aFriendlyScan = {};

	// Non-combatants: civilians, unresolvable characters, neutral third parties.
	//
	// A third store rather than a flag on one of the other two, for the same reason friendlies got
	// their own: these are a different kind of thing. A contact is earned hostile intelligence with
	// a lifecycle. A friendly is on your side and their position is known for free. A non-combatant
	// has to actually be SEEN, and seeing them buys no tactical state at all.
	//
	// Carried across scans rather than rebuilt, so lastObservedPos and the memory window survive a
	// character walking out of the cone.
	protected ref array<ref ACM_TDNonCombatant> m_aNonCombatants = {};
	protected ref array<ref ACM_TDNonCombatant> m_aNonCombatantScan = {};

	// Separate cursor for a separate budget. Sharing the hostile one would let a crowded market
	// starve target acquisition, which is the exact failure the split budget exists to prevent.
	protected int m_iNonCombatantTraceCursor;

	// Ranked hostiles, in presentation order, with the budgets already applied. Rebuilt on the scan
	// tick; the draw pass only walks it.
	//
	// Weak elements. m_aContacts owns the contacts; this borrows them.
	protected ref array<ACM_TDContact> m_aRankedContacts = {};

	// Instrumentation, reset each scan. Cheap enough to keep unconditionally - they are counters,
	// not strings - and they are the difference between "the display feels busy" and a number.
	protected int m_iStatHostiles;
	protected int m_iStatFriendlies;
	protected int m_iStatCivilians;
	protected int m_iStatUnknown;
	protected int m_iStatNeutral;
	protected int m_iStatHostileTraces;
	protected int m_iStatNonCombatantTraces;
	protected int m_iStatFullDetail;
	protected int m_iStatDowngraded;
	protected int m_iStatSuppressed;

	protected bool m_bRoleRegistryWarned;

	protected ref array<string> m_aExcludedFactionKeys = {};
	protected string m_sParsedExclusionSource;
	protected bool m_bTraceFriendlyDraw;
	// MAIN OVERLAY DRAW BUFFERS - TWO OF THEM, ALTERNATED EVERY FRAME.
	//
	// A CanvasWidget handed the same array object every frame can stop presenting it after the menu
	// system cycles the widget layers: the contents change but the reference does not, so nothing
	// tells the canvas its command list is new. That is the shape of the bug where the contact
	// overlay never returned from a menu while the visor overlay - Image and Text widgets, no draw
	// commands - recovered every time, with detection running, contacts present, canvas non-null
	// and commands generated throughout.
	//
	// So the canvas receives a DIFFERENT instance on consecutive updates. The frame's commands are
	// always built into the buffer about to be submitted, never into the one the canvas is still
	// holding - which is why this cannot be done by clearing in place.
	protected ref array<ref CanvasWidgetCommand> m_aDrawCommandsA = {};
	protected ref array<ref CanvasWidgetCommand> m_aDrawCommandsB = {};

	// false = A is this frame's buffer, true = B.
	protected bool m_bDrawBufferB;

	// TEMPORARY - canvas capability probe. Wall-clock time until which a hardcoded shape is drawn
	// after a menu closes. Answers one binary question and nothing else: can this CanvasWidget
	// render ANY draw command at all once a menu has been and gone?
	protected float m_fCanvasProbeUntil;

	// TEMPORARY - final-stage render counters, reset every draw pass.
	protected int m_iDiagCandidates;
	protected int m_iDiagFriendlyScan;
	protected vector m_vDiagCamPos;
	protected int m_iDiagEntering;
	protected int m_iDiagProjected;
	protected int m_iDiagDrawable;
	protected float m_fDiagFirstX;
	protected float m_fDiagFirstY;
	protected float m_fDiagFirstMask;
	protected bool m_bDiagHaveFirst;

	// The feed gets its own buffer rather than reusing the main one. A CanvasWidget keeps
	// rendering whatever list it was last handed, so clearing the main array to build a second
	// pass would blank the main overlay for that frame.
	protected ref array<ref CanvasWidgetCommand> m_aFeedDrawCommands = {};

	// Threat bearing ribbon marks, rebuilt once per scan and read every frame by the visor.
	//
	// A POOL, not a rebuilt list. This array is refilled several times a second for as long as the
	// device is on; allocating a fresh mark object per contact per scan would hand the garbage
	// collector a steady stream of short-lived objects for a readout that is pure arithmetic.
	// Entries above m_iBearingMarkCount are stale and must never be read - the count is the length,
	// the array is only the storage.
	// THE SQUAD ROSTER.
	//
	// Deliberately NOT m_aFriendlies, and the difference is the whole feature.
	//
	// m_aFriendlies answers "which friendlies am I currently drawing a marker on", and everything
	// that shapes it - the view cone, the detection range, the IFF mode, the marker cap - is about
	// what reaches the display. A squadmate behind you is not in it, because you are not drawing a
	// marker on them.
	//
	// This answers "where is my squad", which has none of those qualifiers. It is built from the
	// GROUP rather than from the world scan: enumerated straight off SCR_AIGroup, so it is bounded
	// by squad size instead of by world population and cannot be affected by which way the wearer
	// happens to be facing.
	protected ref array<ref ACM_TDFriendly> m_aSquad = {};

	//! The casualty the wearer is currently looking at, or null. Resolved on the scan tick.
	protected IEntity m_LookedAtCasualty;
	protected ref ACM_VitalsSnapshot m_LookedAtVitals;

	//! Last casualty the ray actually struck, and how long to keep showing them after it stops.
	protected IEntity m_LookedAtHeld;
	protected float m_fLookedAtHoldUntil;

	// Did the roster come from an actual group, or from whoever happened to be standing nearby?
	//
	// Surfaced to the visor rather than kept private, because the two mean different things and a
	// strip that presented them identically would be claiming a squad the wearer does not have.
	protected bool m_bSquadFromGroup;

	protected ref array<ref ACM_IvasBearingMark> m_aBearingMarks = {};
	protected int m_iBearingMarkCount;

	// Which of the two the Add helpers are currently writing into.
	//
	// NOT a ref, and that is the whole point. Both buffers above are owned by their own strong
	// references for the lifetime of this object; this is a plain alias that borrows one of them.
	// Reassigning strong references to each other to swap buffers hands the same array two owners
	// and makes its lifetime depend on assignment order, which is how you free an array a canvas
	// widget is still rendering from.
	protected array<ref CanvasWidgetCommand> m_aActiveCommands;

	protected Widget m_wRoot;
	protected CanvasWidget m_wCanvas;

	// Projection context. Every projection in this file goes through ProjectBodyBox, so redirecting
	// it here is what lets the whole marker pass be re-run through a different camera without any
	// of the drawing code knowing it happened.
	//
	// -1 is the main view, in which case the panel fields are unused and masking falls back to the
	// device's own display mask.
	protected int m_iProjCamera = -1;

	// MAGNIFIED OPTIC (PiP) PROJECTION.
	//
	// A magnified sight does not change the main camera's FOV - it renders the magnified world
	// through a SEPARATE camera into the scope circle. Every projection here went through the main
	// camera, so while looking down a scope the markers were drawn where the target appears to the
	// naked eye while the body on screen was the magnified one: the outlines sat off the target.
	//
	// While a PiP sight is active the whole marker pass is re-pointed at the scope's camera and
	// clipped to the scope circle, so marks land on the magnified body. Marks outside the circle
	// are dropped rather than drawn through the wrong camera - the peripheral image around a scope
	// is the main camera's, and a mark projected by the scope camera would be wrong there.
	protected bool m_bScopeProj;

	// Held for the duration of a draw pass so the clip test can ask the sight itself whether a
	// point is inside the glass, rather than this file reconstructing the circle from a radius and
	// an assumed centre and having to guess which coordinate space each of them is in.
	protected SCR_2DPIPSightsComponent m_ScopeSights;

	// Rate limit for the scoped-highlight probe below. Temporary.
	protected float m_fNextScopeProbe;

	protected bool m_bProjToPanel;
	protected float m_fPanelX;
	protected float m_fPanelY;
	protected float m_fPanelW;
	protected float m_fPanelH;
	protected ref array<TextWidget> m_aLabels = {};

	protected bool m_bRunning;

	//------------------------------------------------------------------------------------------------
	bool IsRunning() { return m_bRunning; }
	ACM_TargetDetectionComponent GetProvider() { return m_Provider; }

	// Round-robin cursor for the line-of-sight trace budget.
	protected int m_iTraceCursor;

	// Broadphase health probe.
	//
	// The sphere query is the cheap path, but if character entities are not registered the
	// way it expects on some build it would just return nothing, forever, and the overlay
	// would sit empty with no clue why. So for the first handful of scans its empty results
	// are cross-checked against the full agent walk. Once it has returned anything at all it
	// is trusted, and an empty result is then taken to mean what it says - nobody nearby -
	// rather than triggering a full walk on every quiet tick, which is exactly the cost the
	// broadphase exists to avoid.
	// Speed bands for the squad status strip, in metres per second.
	//
	// SPRINT sits above a jog rather than at a textbook sprint speed, because the strip is answering
	// "is that one moving hard" rather than measuring anybody. TELEPORT is the discontinuity guard -
	// nothing on foot covers ground that fast, so a sample above it is a move rather than a run.
	protected static const float MOVING_SPEED_MS = 0.7;
	protected static const float SPRINT_SPEED_MS = 4.4;
	protected static const float TELEPORT_SPEED_MS = 30.0;

	protected static const int SPHERE_PROBE_SCANS = 10;
	protected static const int SPHERE_MISS_LIMIT = 3;

	protected int m_iSphereProbesRemaining = SPHERE_PROBE_SCANS;
	protected int m_iSphereMisses;
	protected bool m_bSphereQueryTrusted;
	protected bool m_bSphereQueryBroken;

	// Scratch for the sphere query callback.
	protected ref array<IEntity> m_aQueryResults = {};

	//------------------------------------------------------------------------------------------------
	// The device currently driving detection, or null. Exposed for the data link, which needs the
	// active device's range and sharing settings when it files a report.
	ACM_TargetDetectionComponent GetActiveProvider()
	{
		return m_Provider;
	}

	//------------------------------------------------------------------------------------------------
	// Read-only counts for the radial's status entry. Reported from the STORE, deliberately, not
	// from what is currently drawn - the point of the readout is to tell the wearer what the device
	// knows, which is exactly the number a display budget must not be able to change.
	//------------------------------------------------------------------------------------------------
	int GetContactCount()
	{
		return m_aContacts.Count();
	}

	//------------------------------------------------------------------------------------------------
	int GetLinkContactCount()
	{
		int count = 0;

		foreach (ACM_TDContact contact : m_aContacts)
		{
			if (contact && contact.delivery == ACM_TDDelivery.DATALINK)
				count++;
		}

		return count;
	}

	//------------------------------------------------------------------------------------------------
	// Threat ribbon feed. Borrowed for the length of one draw call and not retained.
	//
	// Read with GetBearingMarkCount(), never with Count() - the array is a pool and holds stale
	// entries past the live count.
	//------------------------------------------------------------------------------------------------
	array<ref ACM_IvasBearingMark> GetBearingMarks()
	{
		return m_aBearingMarks;
	}

	//------------------------------------------------------------------------------------------------
	int GetBearingMarkCount()
	{
		return m_iBearingMarkCount;
	}

	//------------------------------------------------------------------------------------------------
	// The friendly roster the squad status strip reads.
	//
	// Already capped, already sorted by role priority and already carrying resolved role, colour and
	// range - the IFF pass does all of that once a second and then throws the values away after one
	// draw. The strip is reading work that was being done and discarded, which is why it costs
	// nothing.
	//------------------------------------------------------------------------------------------------
	array<ref ACM_TDFriendly> GetFriendlies()
	{
		return m_aFriendlies;
	}

	//------------------------------------------------------------------------------------------------
	// The wearer's own group, everyone inside the roster radius, regardless of sight or facing.
	//
	// What the visor's status strip reads. Separate from GetFriendlies() on purpose - see the note
	// on m_aSquad.
	//------------------------------------------------------------------------------------------------
	array<ref ACM_TDFriendly> GetSquad()
	{
		return m_aSquad;
	}

	//------------------------------------------------------------------------------------------------
	// What the strip should call itself.
	//
	// SQUAD when the roster came from the wearer's group. NEARBY when there was no group to read and
	// it fell back to friendlies in range - which is a real and useful thing to show, but it is not
	// a squad and the panel should not say it is.
	//------------------------------------------------------------------------------------------------
	string GetSquadHeading()
	{
		if (m_bSquadFromGroup)
			return "SQUAD";

		return "NEARBY";
	}

	//------------------------------------------------------------------------------------------------
	static ACM_TargetDetectionSystem GetInstance()
	{
		if (!s_Instance)
			s_Instance = new ACM_TargetDetectionSystem();

		return s_Instance;
	}

	//------------------------------------------------------------------------------------------------
	// Provider lifecycle
	//------------------------------------------------------------------------------------------------
	void Activate(ACM_TargetDetectionComponent provider)
	{
		if (!provider)
			return;

		if (m_aProviders.Find(provider) == -1)
			m_aProviders.Insert(provider);

		// Most recent device wins, so toggling a second pair of goggles does something
		// predictable instead of leaving the first one in charge.
		m_Provider = provider;

		Start();
	}

	//------------------------------------------------------------------------------------------------
	void Deactivate(ACM_TargetDetectionComponent provider)
	{
		int index = m_aProviders.Find(provider);
		if (index != -1)
			m_aProviders.Remove(index);

		if (m_Provider != provider)
			return;

		// Hand over to whatever is still lit, otherwise shut down.
		m_Provider = null;
		foreach (ACM_TargetDetectionComponent candidate : m_aProviders)
		{
			if (candidate && candidate.IsDetectionActive())
			{
				m_Provider = candidate;
				break;
			}
		}

		if (m_Provider)
		{
			Start();
			return;
		}

		Stop();
	}

	//------------------------------------------------------------------------------------------------
	protected void Start()
	{
		// A dedicated server has no workspace and nothing to draw to.
		if (!GetGame() || !GetGame().GetWorkspace())
			return;

		if (!GetGame().GetCallqueue())
			return;

		int interval = 100;
		if (m_Provider)
			interval = m_Provider.GetScanIntervalMs();

		// Always remove first - Start() is re-entered when providers change and we must
		// not stack duplicate timers.
		GetGame().GetCallqueue().Remove(ScanTick);
		GetGame().GetCallqueue().Remove(DrawTick);

		GetGame().GetCallqueue().CallLater(ScanTick, interval, true);
		GetGame().GetCallqueue().CallLater(DrawTick, 0, true);

		m_bRunning = true;
		m_iTraceCursor = 0;

		// Populate immediately so the first frame after switching on already has contacts
		// rather than showing nothing for one scan interval.
		ScanTick();
	}

	//------------------------------------------------------------------------------------------------
	protected void Stop()
	{
		m_bRunning = false;

		if (GetGame() && GetGame().GetCallqueue())
		{
			GetGame().GetCallqueue().Remove(ScanTick);
			GetGame().GetCallqueue().Remove(DrawTick);
		}

		m_aContacts.Clear();
		m_aFriendlies.Clear();
		m_aFriendlyScan.Clear();
		m_aSquad.Clear();
		m_aNonCombatants.Clear();
		m_aNonCombatantScan.Clear();
		m_aRankedContacts.Clear();
		m_iBearingMarkCount = 0;
		ACM_IvasEventLog.Clear();
		ClearOverlay();

		// A mesh highlight is sticky where the engine outliner is per-frame. The expiry sweep would
		// clear these within half a second anyway, but a hard stop should not leave a lit enemy
		// standing there while a timer gets round to it.
		ACM_ScopedHighlightManager.GetInstance().ExtinguishAll();
	}

	//------------------------------------------------------------------------------------------------
	// Force everything down. Called from the NVG component's hard teardown paths (death,
	// unequip) so nothing can survive into a respawn.
	//------------------------------------------------------------------------------------------------
	void ForceShutdown()
	{
		m_aProviders.Clear();
		m_Provider = null;
		Stop();
		m_aFriendlies.Clear();
		m_aFriendlyScan.Clear();
		m_aSquad.Clear();
		m_aNonCombatants.Clear();
		m_aNonCombatantScan.Clear();
	}

	//------------------------------------------------------------------------------------------------
	// Drop everything the local sensor has acquired.
	//
	// Internal and debug-facing only. Deliberately NOT offered as a player control: it manipulates
	// store state rather than operating the device, and anything that arrived over the data link
	// re-arrives on the next report, so from the player's side the button would look broken.
	//
	// It cannot be used to hide a contact either - shared contacts come straight back.
	//------------------------------------------------------------------------------------------------
	void ClearLocalContacts()
	{
		m_aContacts.Clear();
		m_aRankedContacts.Clear();
		m_aNonCombatants.Clear();
		m_iBearingMarkCount = 0;
	}

	//------------------------------------------------------------------------------------------------
	// Scan pass
	//------------------------------------------------------------------------------------------------
	protected void ScanTick()
	{
		// Heartbeat for the diagnostic below. ScanTick and DrawTick are two independent repeating
		// callqueue entries, so one can die while the other lives - and the visible result of a
		// dead ScanTick is an overlay that reports itself healthy and draws an empty world.
		m_fLastScanTime = System.GetTickCount() / 1000.0;

		if (!m_bRunning || !m_Provider)
			return;

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return;

		IEntity localEntity = SCR_PlayerController.GetLocalControlledEntity();
		if (!localEntity)
		{
			m_aContacts.Clear();
			return;
		}

		vector camMat[4];
		world.GetCurrentCamera(camMat);
		vector camPos = camMat[3];
		vector camForward = camMat[2];

		float maxRange = m_Provider.GetMaxRange();
		// Compare cosines rather than calling Acos per candidate.
		float halfFovCos = Math.Cos((m_Provider.GetFovDegrees() * 0.5) * Math.DEG2RAD);

		// ONE sphere query serves both the hostile scan and the squad roster, widened to whichever
		// needs to reach further. The roster radius is routinely larger than the hostile detection
		// range - knowing where your squad is stays useful well past the range at which spotting an
		// enemy does - and gathering only to maxRange would cut the roster short at a distance that
		// has nothing to do with squads.
		//
		// The extra candidates cost the hostile loop one distance comparison each; it rejects
		// anything past maxRange on its first test.
		float gatherRange = Math.Max(maxRange, m_Provider.GetSquadRosterRange());

		array<IEntity> candidates = GatherCandidates(world, localEntity, camPos, gatherRange);

		// TEMPORARY - the scan's own inputs and yield. If candidates goes to zero after a menu the
		// broadphase or the camera is the fault, not anything downstream of it.
		m_iDiagCandidates = candidates.Count();
		m_vDiagCamPos = camPos;

		float now = System.GetTickCount() / 1000.0;
		float memory = m_Provider.GetMemorySeconds();

		// Classification tracing runs as one complete pass a second rather than per scan, so
		// the log reads as a snapshot of every candidate at a moment in time instead of an
		// unbounded stream. The header carries the two switches that decide whether IVAS can
		// engage at all - if ivasSetting reads 0 the device attribute is off and no contact
		// will ever be marked, which is a configuration answer rather than a code one.
		bool logClassify = false;
		if (ACM_TD_DEBUG && now >= m_fNextClassifyLogTime)
		{
			logClassify = true;
			m_fNextClassifyLogTime = now + 1.0;
			Print(string.Format("[ACM TD] ---- classify pass: %1 candidate(s), viewerFaction=%2, ivasSetting=%3, factionColors=%4 ----",
				candidates.Count(),
				DebugFactionKey(SCR_Faction.GetEntityFaction(localEntity)),
				m_Provider.UsesIvasHostileContacts(),
				m_Provider.UsesFactionColors()), LogLevel.DEBUG);
		}

		// Candidates inside the sensor envelope this scan. An IVAS contact that has already
		// been acquired is NOT required to be in here - once acquired it lives in the store
		// until it expires, whether or not the wearer is still looking at it.
		array<ref ACM_TDContact> inEnvelope = {};

		m_aFriendlyScan.Clear();
		m_aNonCombatantScan.Clear();

		m_iStatHostiles = 0;
		m_iStatFriendlies = 0;
		m_iStatCivilians = 0;
		m_iStatUnknown = 0;
		m_iStatNeutral = 0;
		m_iStatHostileTraces = 0;
		m_iStatNonCombatantTraces = 0;
		m_iStatFullDetail = 0;
		m_iStatDowngraded = 0;
		m_iStatSuppressed = 0;

		foreach (IEntity candidate : candidates)
		{
			if (!candidate || candidate == localEntity)
				continue;

			vector targetPos = candidate.GetOrigin();
			float distance = vector.Distance(camPos, targetPos);
			if (distance > maxRange)
				continue;

			// FOV cone. Cheap, so it runs before any raycast.
			vector toTarget = targetPos - camPos;
			float length = toTarget.Length();
			if (length > 0.01)
			{
				vector direction = toTarget / length;
				if (vector.Dot(direction, camForward) < halfFovCos)
					continue;
			}

			if (!PassesLifeStateFilter(candidate))
				continue;

			ACM_TDAffiliation affiliation = ResolveAffiliation(candidate, localEntity);

			// The fork, and it happens BEFORE any contact is created or any raycast is budgeted.
			//
			// Friendlies leave the hostile pipeline here entirely. They never become contacts, so
			// they never consume a trace, never accumulate dwell and never carry a confidence
			// value.
			//
			// Civilians, unresolvable characters and neutrals leave here too, and go to a third
			// store of their own. They used to get nothing at all, which was honest about the
			// device's authority and useless to a player trying not to shoot a farmer. What they
			// get now is IDENTIFICATION and nothing else: a glyph where the device last actually
			// saw them, with no dwell, no confidence, no silhouette and no route to the data link.
			//
			// The point of forking here rather than filtering later is that it is structural. A
			// non-hostile never reaches the code that could acquire it, so no flag anyone forgets
			// to clear can turn a civilian into a target.
			if (logClassify)
				LogCandidate(candidate, affiliation, distance, localEntity);

			if (affiliation != ACM_TDAffiliation.HOSTILE)
			{
				if (affiliation == ACM_TDAffiliation.FRIENDLY)
				{
					m_iStatFriendlies++;
					ConsiderFriendly(candidate, distance, now, logClassify);
				}
				else
				{
					if (affiliation == ACM_TDAffiliation.CIVILIAN)
						m_iStatCivilians++;
					else if (affiliation == ACM_TDAffiliation.UNKNOWN)
						m_iStatUnknown++;
					else
						m_iStatNeutral++;

					ConsiderNonCombatant(candidate, affiliation, distance);
				}

				continue;
			}

			// Hostile acquisition switched off at the device. Classification has already happened
			// and shared contacts keep arriving and being stored - this stops the local sensor
			// ACQUIRING, which is a different thing from making the device deaf.
			if (!m_Provider.IsHostileTrackingEnabled())
				continue;

			m_iStatHostiles++;

			// Replicated id where there is one. This is the contact's cross-machine identity,
			// so it is resolved once here rather than at the point a second sensor shows up.
			int targetId = Replication.FindItemId(candidate);

			ACM_TDContact contact = FindExistingContact(candidate, targetId);
			if (!contact)
				contact = new ACM_TDContact(candidate);

			// Re-bound every scan, not just at creation. A contact first filed by a drone has
			// no entity handle on this client; the moment the wearer's own scan picks the same
			// target up, that handle arrives here and the existing contact adopts it instead of
			// a duplicate being created alongside it.
			contact.entity = candidate;
			contact.targetId = targetId;

			// Re-classified EVERY scan, not once at creation - the affiliation resolved above is
			// this scan's answer, not a remembered one.
			//
			// This was half of the original dispatch leak. Classification used to run only inside
			// the !contact branch, so whatever affiliation resolved on the very first scan was
			// permanent. A character that has just streamed in often has no readable faction yet,
			// which resolved to UNKNOWN and stuck for the contact's entire life.
			contact.affiliation = affiliation;
			contact.ivas = m_Provider.UsesIvasHostileContacts();

			contact.distance = distance;

			// Logged after the distance write on purpose. Logging above it reported the
			// PREVIOUS scan's distance for an existing contact and 0 for a new one, which
			// made the trace look broken when it was only early.
			if (logClassify)
				LogClassification(contact, GetTargetFaction(candidate));

			inEnvelope.Insert(contact);
		}

		SortByDistance(inEnvelope);

		// Trim to the cap before doing any further per-contact work.
		while (inEnvelope.Count() > m_Provider.GetMaxTargets())
			inEnvelope.Remove(inEnvelope.Count() - 1);

		ResolveLineOfSight(world, camPos, inEnvelope, now, memory);

		// Apply the IVAS lifecycle to everything the traces just touched: dwell, the position
		// freeze, and confidence. Contacts NOT in the envelope this scan are handled by the
		// carry-over pass below, which is what lets an acquired contact survive the wearer
		// turning away from it.
		foreach (ACM_TDContact traced : inEnvelope)
			UpdateIvasState(traced, now);

		array<ref ACM_TDContact> keep = {};

		// 1. Envelope contacts that earned their place.
		foreach (ACM_TDContact contact : inEnvelope)
		{
			if (!contact.traced)
				continue;

			if (contact.ivas)
			{
				// A candidate part-way through its dwell is NOT yet a contact and is not
				// drawn - but it has to survive in the store regardless.
				//
				// This store is rebuilt from `keep` every scan, and FindExistingContact only
				// searches the store. Dropping a candidate that has not finished dwelling
				// therefore destroys the very object holding its dwell timer: the next scan
				// finds nothing, builds a fresh contact with dwellStart back at -1, and the
				// clock restarts from zero forever. Acquisition could never complete, so no
				// hostile could ever be drawn, while friendlies - which run the legacy keep
				// rule below - appeared instantly.
				//
				// Retained only while sight holds. Losing sight resets dwell to -1 anyway
				// (see BreakSight), so an unacquired contact with no line of sight has no
				// state left worth carrying and is dropped.
				if (!contact.acquired)
				{
					if (contact.hasLineOfSight)
						keep.Insert(contact);

					continue;
				}

				// The expiry check matters here as well as in the carry-over pass below: a
				// target can stay inside the cone and inside range while staying behind a
				// wall the whole time, and that contact has to age out like any other.
				if (!IsExpired(contact, now))
					keep.Insert(contact);
				else
					ACM_IvasEventLog.Push(ACM_EIvasEventKind.LOST, string.Format("CONTACT LOST  LAST %1m", Math.Round(contact.distance)));

				continue;
			}

			// Legacy path, unchanged.
			if (contact.hasLineOfSight || (now - contact.lastSeenTime) <= memory)
				keep.Insert(contact);
		}

		// 2. Acquired IVAS contacts that fell out of the envelope - target left the cone,
		//    walked out of range, or was trimmed by the target cap. Sight is lost by
		//    definition, so they freeze and decay exactly as if a wall had intervened.
		foreach (ACM_TDContact carried : m_aContacts)
		{
			if (!carried || !carried.ivas || !carried.acquired)
				continue;

			if (inEnvelope.Find(carried) != -1)
				continue;

			BreakSight(carried, now);

			if (!IsExpired(carried, now))
				keep.Insert(carried);
			else
				ACM_IvasEventLog.Push(ACM_EIvasEventKind.LOST, string.Format("CONTACT LOST  LAST %1m", Math.Round(carried.distance)));
		}

		foreach (ACM_TDContact contact : keep)
			FinaliseContact(contact, localEntity);

		m_aContacts = keep;

		// Friendlies: sort by role priority, then cap. Capping AFTER the sort is what makes the cap
		// survivable - when a full platoon is in view the leaders and specialists are the ones kept
		// and the riflemen are the ones dropped, rather than whoever happened to be gathered first.
		SortFriendliesByPriority(m_aFriendlyScan);

		while (m_aFriendlyScan.Count() > m_Provider.GetMaxFriendlies())
			m_aFriendlyScan.Remove(m_aFriendlyScan.Count() - 1);

		array<ref ACM_TDFriendly> keptFriendlies = {};
		foreach (ACM_TDFriendly friendly : m_aFriendlyScan)
			keptFriendlies.Insert(friendly);

		m_aFriendlies = keptFriendlies;
		m_iDiagFriendlyScan = m_aFriendlies.Count();

		// Non-combatants: their own trace budget, their own cap, their own memory window. None of
		// this shares a number with the hostile path.
		FinaliseNonCombatants(world, camPos, now);

		// Presentation last, once every store is settled. Priority is cached here and read - never
		// recomputed - by the draw pass.
		RankContactsForPresentation(now);

		// Group first, and only falls back to `candidates` when there is no group to read.
		BuildSquadRoster(now, candidates, localEntity);

		// Ribbon marks last of all, so they read the settled stores rather than a half-rebuilt one.
		BuildBearingMarks();

		if (logClassify)
			LogScanCounters();
	}

	//------------------------------------------------------------------------------------------------
	// SQUAD ROSTER
	//
	// Everyone the wearer counts as their squad inside the roster radius, whether or not the wearer
	// can see them and whichever way they are facing.
	//
	// TWO SOURCES, in order of authority.
	//
	// A GROUP is the real answer when there is one. Enumerated from the group itself rather than
	// filtered out of the world scan, because the scan's candidate list is shaped by a detection
	// range and a view cone and any roster built from it would lose a squadmate the moment the
	// wearer turned around.
	//
	// PROXIMITY is the fallback when the group is absent or empty - most of Game Master, Workbench,
	// and any scenario that does not assign one. The strip renames itself in that case rather than
	// presenting strangers as a squad; see GetSquadHeading().
	//
	// This does NOT widen what the wearer can see. It reports friendly positions, which this system
	// has always drawn live and has always been allowed to - a squadmate's position is not
	// intelligence gathered by a sensor, it is their own client saying where they are. No hostile,
	// no unknown and no non-combatant can reach this path; there is no branch here that looks at
	// one.
	//------------------------------------------------------------------------------------------------
	protected void BuildSquadRoster(float now, array<IEntity> candidates, IEntity localEntity)
	{
		if (!ACM_IffServerSettings.Allows(ACM_IffServerSettings.FRIENDLY))
		{
			m_aSquad.Clear();
			m_LookedAtCasualty = null;
			m_bSquadFromGroup = false;
			return;
		}

		array<ref ACM_TDFriendly> roster = {};

		vector viewer = GetViewerOrigin();
		float range = m_Provider.GetSquadRosterRange();

		m_bSquadFromGroup = false;

		// May legitimately be null - the wearer is in no group at all, which is the common case
		// outside a scenario that assigns one. The group walk is skipped entirely rather than
		// guarded call by call.
		SCR_AIGroup group = ResolveOwnGroup();
		if (group)
			GatherGroupMembers(roster, group, localEntity, viewer, range, now);

		// TWO BLOCKS, NOT ONE LIST.
		//
		// The group block is unconditional: everyone in your group is on the strip whether or not you
		// can see them, because that is the question the roster answers. It is sorted by role priority
		// and pinned to the top.
		//
		// Below it come friendlies the wearer can currently SEE, taken from the marker store rather
		// than re-derived, because that store is already shaped by the view cone, the range and the IFF
		// mode - it is the definitive answer to "in view" and re-implementing the test here would give
		// two answers that could disagree.
		if (!roster.IsEmpty())
			m_bSquadFromGroup = true;

		foreach (ACM_TDFriendly member : roster)
		{
			if (member)
				member.inSquad = true;
		}

		SortFriendliesByPriority(roster);

		AppendVisibleFriendlies(roster, localEntity);

		// No group AND nothing in view. Proximity is better than a blank panel, and GetSquadHeading()
		// already renames the strip so it is not claiming a chain of command that does not exist.
		if (roster.IsEmpty())
		{
			GatherNearbyFriendlies(roster, candidates, localEntity, viewer, range, now);
			SortFriendliesByPriority(roster);
		}

		SampleRosterVitals(roster);

		m_aSquad = roster;
		ResolveLookedAtCasualty(localEntity);
	}

	//------------------------------------------------------------------------------------------------
	// The wearer's own group, walked outright. A dozen members at most, so this costs less than
	// filtering the scan would have.
	//------------------------------------------------------------------------------------------------
	//------------------------------------------------------------------------------------------------
	//! The casualty under the wearer's gaze, or null. Read by the visor overlay to raise its panel.
	IEntity GetLookedAtCasualty()
	{
		return m_LookedAtCasualty;
	}

	//------------------------------------------------------------------------------------------------
	//! Vitals for whoever GetLookedAtCasualty() returned. Null-safe: valid is false when nobody is.
	ACM_VitalsSnapshot GetLookedAtVitals()
	{
		return m_LookedAtVitals;
	}

	//------------------------------------------------------------------------------------------------
	//! Where the casualty is on screen, in SCALED pixels, ready to anchor the callout against.
	//!
	//! Aimed at the head rather than the origin so the panel hangs off the top of a body rather than
	//! sinking into the ground beside a prone one. Returns false when there is nobody or they are
	//! behind the camera.
	bool GetLookedAtScreenPos(out float outX, out float outY)
	{
		outX = 0;
		outY = 0;

		if (!m_LookedAtCasualty)
			return false;

		WorkspaceWidget workspace = GetGame().GetWorkspace();
		BaseWorld world = GetGame().GetWorld();
		if (!workspace || !world)
			return false;

		vector head = m_LookedAtCasualty.GetOrigin() + Vector(0, LOOKAT_ANCHOR_HEIGHT_M, 0);
		vector screen = workspace.ProjWorldToScreen(head, world);

		// Behind the camera. Cannot happen while the ray is hitting them, but the panel outlives the
		// pick by a frame or two and a callout flipped to the far side of the screen looks like a bug.
		if (screen[2] < 0.01)
			return false;

		outX = workspace.DPIScale(screen[0]);
		outY = workspace.DPIScale(screen[1]);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	// WHO AM I LOOKING AT.
	//
	// A tight cone off the camera axis, line of sight, and a range limit - the same three tests the
	// marker path already uses, and TraceVisibility is reused rather than re-implemented so the panel
	// can never disagree with the marker about whether someone is visible.
	//
	// Tight rather than generous on purpose. A wide cone picks a different casualty every time the
	// wearer breathes, and a panel that flickers between two men lying next to each other is worse
	// than no panel. The best dot wins, so with several in the cone it is the one nearest the centre
	// of view rather than the nearest in metres.
	//
	// SUPPRESSED WITH THE WEAPON UP. A medical panel is a triage tool, not something to read down the
	// sights - it would sit over the target while the wearer is being shot at. Lower the weapon and
	// it returns.
		//------------------------------------------------------------------------------------------------
	//! One look-at diagnostic, printed only when it says something new.
	//!
	//! These ran once a second unconditionally and buried the console under thousands of identical
	//! lines, which is worse than no diagnostic at all - a log nobody can read is a log nobody reads.
	//! A state that has not changed has nothing to report.
	protected void LookAtDiag(string message)
	{
		if (message == m_sLastLookAtDiag)
			return;

		m_sLastLookAtDiag = message;
		Print("[ACM LOOKAT] " + message, LogLevel.NORMAL);
	}

//------------------------------------------------------------------------------------------------
	protected void ResolveLookedAtCasualty(IEntity localEntity)
	{
		m_LookedAtCasualty = null;

		if (!m_LookedAtVitals)
			m_LookedAtVitals = new ACM_VitalsSnapshot();

		m_LookedAtVitals.Reset();

		if (!localEntity)
			return;

		bool diag = false;
		if (ACM_LOOKAT_DEBUG)
		{
			float dnow = System.GetTickCount() / 1000.0;
			if (dnow >= m_fNextLookAtDiag)
			{
				m_fNextLookAtDiag = dnow + 1.0;
				diag = true;
			}
		}

		if (m_bLookAtSuppressWeaponRaised)
		{
			SCR_CharacterControllerComponent mine = SCR_CharacterControllerComponent.Cast(localEntity.FindComponent(SCR_CharacterControllerComponent));
			if (mine && mine.IsWeaponRaised())
			{
				if (diag)
					LookAtDiag("suppressed: weapon raised");

				return;
			}
		}

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return;

		vector camMat[4];
		world.GetCurrentCamera(camMat);

		vector camPos = camMat[3];
		vector forward = camMat[2];

		float now = System.GetTickCount() / 1000.0;

		IEntity hit = PickLookAtCharacter(world, camPos, forward, localEntity);
		bool struck = (hit != null);

		// STICKY. The ray missing does not mean the wearer stopped looking - it means a limb moved, or
		// they breathed. Fall back to whoever was last struck until the hold lapses.
		if (!hit && m_LookedAtHeld && now < m_fLookedAtHoldUntil)
			hit = m_LookedAtHeld;

		if (!hit)
		{
			m_LookedAtHeld = null;

			if (diag)
				LookAtDiag("nobody under the crosshair");

			return;
		}

		ACM_VitalsService service = ACM_VitalsService.GetInstance();
		if (!service || !service.Read(hit, m_LookedAtVitals))
		{
			if (diag)
				LookAtDiag("found a character but vitals are unreadable");

			return;
		}

		// Healthy people do not need a medical panel raised over them.
		if (m_LookedAtVitals.state == ACM_EVitalState.HEALTHY)
		{
			m_LookedAtHeld = null;

			if (diag)
				LookAtDiag("looking at an uninjured character");

			return;
		}

		m_LookedAtCasualty = hit;

		// Anyone under the gaze is sampled too - they may not be on the strip at all.
		ACM_MedicationLog.Observe(hit);

		// Only a genuine strike extends the hold. Refreshing it from the held fallback would make the
		// panel immortal once raised.
		if (struck)
		{
			m_LookedAtHeld = hit;
			m_fLookedAtHoldUntil = now + LOOKAT_HOLD_SECONDS;
		}

		if (diag)
		{
			string d = "casualty=";
			d += m_LookedAtVitals.StateLabel();
			d += "/";
			d += m_LookedAtVitals.SeverityLabel();
			d += " wounds=";
			d += m_LookedAtVitals.injuries.Count().ToString();
			LookAtDiag(d);
		}
	}

	//------------------------------------------------------------------------------------------------
	// Ray down the view axis; returns the CHARACTER it struck, or null.
	//
	// Deliberately independent of the roster. The panel is its own system: it answers "who am I
	// looking at", which is a different question from "who is on my strip", and tying it to the strip
	// meant anyone past the row cap - or any casualty who is not a visible friendly - could never
	// raise one no matter how squarely you were staring at them.
	//
	// The collider struck is usually a child of the character rather than the character itself, so the
	// hit is walked up its parents until something with a character controller turns up.
		//------------------------------------------------------------------------------------------------
	// WHO IS THE WEARER LOOKING AT?
	//
	// NOT A PHYSICS RAY, AND THAT WAS THE BUG.
	//
	// The first version fired TraceMove down the camera forward and read back whatever it struck.
	// Standing directly over a bleeding casualty it reported "ray hit no character" on every single
	// tick, and the reason is that a character's collision bodies do not live on the Default physics
	// layer. The ray went straight through her and struck the floor underneath, so the trace was
	// working perfectly and answering a question nobody asked. Widening the layer mask would have
	// papered over it, at the cost of a pick that also depends on ragdoll bodies being present and
	// awake - which for a downed man is exactly when they are least dependable.
	//
	// So selection is geometric instead. Every character within reach is asked for, each is scored by
	// how far off the crosshair he sits, and the best one wins. The broadphase this uses is the same
	// one the detection scan already trusts to find friendlies, so if it can see them the panel can.
	//
	// The physics trace still earns its place, but only for the job it is actually good at: once a
	// candidate is chosen, one WORLD-only ray decides whether a wall is in the way. Terrain and
	// buildings are unambiguously world geometry, so that test has none of the layer trouble the
	// original had.
	//
	// SCORING, AND WHY IT IS NOT JUST THE NEAREST.
	//
	// Nearest-to-crosshair beats nearest-to-wearer for a medical readout. Two men lying together and
	// the wearer picks by aim, not by shuffling their feet. Ties go to the closer of the two, since
	// with the crosshair genuinely between them the reachable one is the one you meant.
	//------------------------------------------------------------------------------------------------
	protected IEntity PickLookAtCharacter(BaseWorld world, vector camPos, vector forward, IEntity viewer)
	{
		m_aLookAtQuery.Clear();
		world.QueryEntitiesBySphere(camPos, LOOKAT_RANGE_M + LOOKAT_QUERY_PAD_M, AddLookAtCandidate, FilterLookAtCandidate, EQueryEntitiesFlags.ALL);

		if (m_aLookAtQuery.IsEmpty())
			return null;

		// Range is the wearer's, not the camera's. In third person the camera floats metres behind
		// him, and a limit that moved with it would mean the same casualty is readable or not
		// depending on which view key was last pressed.
		vector viewerPos = camPos;
		if (viewer)
			viewerPos = viewer.GetOrigin();

		IEntity best = null;
		float bestMiss = LOOKAT_CONE_DEG;
		float bestDist = 0;

		foreach (IEntity candidate : m_aLookAtQuery)
		{
			if (!candidate || candidate == viewer)
				continue;

			vector origin = candidate.GetOrigin();

			if (vector.Distance(viewerPos, origin) > LOOKAT_RANGE_M)
				continue;

			// FRIENDLIES ONLY.
			//
			// The panel reads blood volume, bleed rate and which limb is open. On an enemy that is
			// not a medical aid, it is a damage report telling you whether to push or reload, and
			// there is no version of this feature that should hand you that. The same gate the
			// friendly roster uses, so the two can never disagree about who is on your side.
			if (ResolveAffiliation(candidate, viewer) != ACM_TDAffiliation.FRIENDLY)
				continue;

			// Two aim points, better of the two. A standing man is best described by his chest and a
			// prone one by his origin, and a system that raises a medical panel has no business
			// assuming the casualty is upright.
			float miss = AngularMiss(camPos, forward, origin + Vector(0, LOOKAT_ANCHOR_HEIGHT_M, 0));

			float missChest = AngularMiss(camPos, forward, origin + Vector(0, TRACE_AIM_HEIGHT_M, 0));
			if (missChest < miss)
				miss = missChest;

			if (miss > bestMiss)
				continue;

			float dist = vector.Distance(camPos, origin);

			// Equal aim, closer man wins.
			if (best && miss >= bestMiss && dist >= bestDist)
				continue;

			best = candidate;
			bestMiss = miss;
			bestDist = dist;
		}

		if (!best)
			return null;

		if (!HasLineOfSight(world, camPos, best, viewer))
			return null;

		return best;
	}

	//------------------------------------------------------------------------------------------------
	//! Degrees between the view direction and a point, less the body's own angular radius at that
	//! range. Zero or below means the crosshair is on the body rather than merely near it.
	protected float AngularMiss(vector camPos, vector forward, vector point)
	{
		vector delta = point - camPos;

		float dist = delta.Length();
		if (dist < 0.01)
			return 0;

		float dot = Math.Clamp(vector.Dot(delta.Normalized(), forward), -1.0, 1.0);
		float angle = Math.Acos(dot) * Math.RAD2DEG;

		// Behind the wearer. No amount of body radius rescues that.
		if (dot <= 0)
			return 180.0;

		float bodyAngle = Math.Atan2(LOOKAT_BODY_RADIUS_M, dist) * Math.RAD2DEG;

		float miss = angle - bodyAngle;
		if (miss < 0)
			miss = 0;

		return miss;
	}

	//------------------------------------------------------------------------------------------------
	//! One WORLD-only ray to the candidate's chest. Terrain and buildings block; nothing else does.
	protected bool HasLineOfSight(BaseWorld world, vector camPos, notnull IEntity target, IEntity viewer)
	{
		vector aim = target.GetOrigin() + Vector(0, LOOKAT_ANCHOR_HEIGHT_M, 0);

		float dist = vector.Distance(camPos, aim);
		if (dist < 0.5)
			return true;

		TraceParam param = new TraceParam();
		param.Start = camPos;
		param.End = aim;
		param.Flags = TraceFlags.WORLD;

		array<IEntity> exclude = {};
		if (viewer)
			exclude.Insert(viewer);

		exclude.Insert(target);
		param.ExcludeArray = exclude;

		float fraction = world.TraceMove(param, null);

		// A little slack at the far end: the aim point sits inside the body, and ground under a prone
		// casualty is close enough to it that an exact test would call every downed man occluded.
		return fraction >= 0.93;
	}

	//------------------------------------------------------------------------------------------------
	protected bool FilterLookAtCandidate(IEntity entity)
	{
		return ChimeraCharacter.Cast(entity) != null;
	}

	//------------------------------------------------------------------------------------------------
	protected bool AddLookAtCandidate(IEntity entity)
	{
		if (entity)
			m_aLookAtQuery.Insert(entity);

		return true;
	}
	//------------------------------------------------------------------------------------------------
	// Everyone the wearer can currently see who is not already in the group block.
	//
	// Reads the marker store, which is the same set being drawn on screen this frame. That is
	// deliberate: "in view" then means exactly what the wearer observes, and a name cannot appear on
	// the strip without a marker on the body, or vice versa.
	//------------------------------------------------------------------------------------------------
	protected void AppendVisibleFriendlies(notnull array<ref ACM_TDFriendly> roster, IEntity localEntity)
	{
		array<ref ACM_TDFriendly> visible = {};

		foreach (ACM_TDFriendly seen : m_aFriendlies)
		{
			if (!seen || !seen.entity || seen.entity == localEntity)
				continue;

			if (RosterContains(roster, seen.entity))
				continue;

			seen.inSquad = false;
			visible.Insert(seen);
		}

		// Nearest first. Role priority orders the squad block because that is a chain of command;
		// this block has none, so proximity is the only ordering that means anything.
		SortVisibleByDistance(visible);

		foreach (ACM_TDFriendly entry : visible)
			roster.Insert(entry);
	}

	//------------------------------------------------------------------------------------------------
	protected bool RosterContains(notnull array<ref ACM_TDFriendly> roster, IEntity entity)
	{
		foreach (ACM_TDFriendly member : roster)
		{
			if (member && member.entity == entity)
				return true;
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected void SortVisibleByDistance(notnull array<ref ACM_TDFriendly> friendlies)
	{
		int count = friendlies.Count();

		for (int i = 1; i < count; i++)
		{
			ref ACM_TDFriendly key = friendlies[i];
			int j = i - 1;

			while (j >= 0 && friendlies[j].distance > key.distance)
			{
				friendlies[j + 1] = friendlies[j];
				j--;
			}

			friendlies[j + 1] = key;
		}
	}

	//------------------------------------------------------------------------------------------------
	// Casualty state for the whole strip, once per scan tick.
	//
	// On the scan interval rather than per frame, for the same reason the label and stance are: a
	// wound does not change sixty times a second, and walking a damage manager per person per frame
	// to render the same word would dwarf everything else this system does.
	//------------------------------------------------------------------------------------------------
	protected void SampleRosterVitals(notnull array<ref ACM_TDFriendly> roster)
	{
		ACM_VitalsService service = ACM_VitalsService.GetInstance();
		if (!service)
			return;

		foreach (ACM_TDFriendly member : roster)
		{
			if (!member || !member.entity)
				continue;

			if (!member.vitals)
				member.vitals = new ACM_VitalsSnapshot();

			service.Read(member.entity, member.vitals);

			// Treatment timeline. Sampled here rather than when the panel opens, so a casualty treated
			// while on the strip has a history by the time anyone looks at them.
			ACM_MedicationLog.Observe(member.entity);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void GatherGroupMembers(notnull array<ref ACM_TDFriendly> roster, notnull SCR_AIGroup group, IEntity localEntity, vector viewer, float range, float now)
	{
		PlayerManager players = GetGame().GetPlayerManager();

		// Players first, so a human squadmate is never the one dropped if anything ever caps this.
		if (players)
		{
			array<int> memberIds = group.GetPlayerIDs();
			if (memberIds)
			{
				foreach (int memberId : memberIds)
				{
					IEntity member = players.GetPlayerControlledEntity(memberId);

					// Not streamed in on this client. We genuinely do not know where they are, so
					// the strip says nothing about them rather than inventing a position.
					if (!member || member == localEntity)
						continue;

					ConsiderSquadMember(roster, member, memberId, viewer, range, now);
				}
			}
		}

		array<AIAgent> agents = {};
		group.GetAgents(agents);

		foreach (AIAgent agent : agents)
		{
			if (!agent)
				continue;

			IEntity member = agent.GetControlledEntity();
			if (!member || member == localEntity)
				continue;

			// A player-controlled member already came through the pass above. Adding them again
			// here would put the same squadmate on the strip twice.
			int agentPlayerId = -1;
			if (players)
				agentPlayerId = players.GetPlayerIdFromControlledEntity(member);

			if (agentPlayerId > 0)
				continue;

			ConsiderSquadMember(roster, member, -1, viewer, range, now);
		}
	}

	//------------------------------------------------------------------------------------------------
	// Fallback roster: friendlies within the radius, whatever group they are or are not in.
	//
	// Built from the scan's own candidate list, which is a sphere query plus every player and is NOT
	// view-cone filtered - the cone test happens per candidate later in the scan, so reading the
	// list here gets everyone around the wearer rather than everyone in front of them. That is the
	// whole reason the strip is not simply reading m_aFriendlies.
	//------------------------------------------------------------------------------------------------
	protected void GatherNearbyFriendlies(notnull array<ref ACM_TDFriendly> roster, array<IEntity> candidates, IEntity localEntity, vector viewer, float range, float now)
	{
		if (!candidates)
			return;

		PlayerManager players = GetGame().GetPlayerManager();

		foreach (IEntity candidate : candidates)
		{
			if (!candidate || candidate == localEntity)
				continue;

			if (!PassesLifeStateFilter(candidate))
				continue;

			// Classified through the SAME resolver the markers use, so the strip and the markers can
			// never disagree about whose side somebody is on. Friendly only - there is no branch
			// here that admits a hostile, an unknown or a non-combatant.
			if (ResolveAffiliation(candidate, localEntity) != ACM_TDAffiliation.FRIENDLY)
				continue;

			int playerId = -1;
			if (players)
				playerId = players.GetPlayerIdFromControlledEntity(candidate);

			ConsiderSquadMember(roster, candidate, playerId, viewer, range, now);
		}
	}

	//------------------------------------------------------------------------------------------------
	// One candidate for the roster. Reuses ACM_TDFriendly so the status, speed and colour resolution
	// written for the marker path applies here unchanged rather than being duplicated.
	//------------------------------------------------------------------------------------------------
	protected void ConsiderSquadMember(notnull array<ref ACM_TDFriendly> roster, IEntity member, int playerId, vector viewer, float range, float now)
	{
		// Both callers already check this. Kept anyway: this method dereferences the entity on its
		// first line, and a roster gatherer added later would fail here rather than at its own
		// missing guard.
		if (!member)
			return;

		float distance = vector.Distance(viewer, member.GetOrigin());
		if (distance > range)
			return;

		// Carried over from last scan when possible, because the speed sample and its baseline live
		// on the record. A fresh record every scan would reset the baseline every scan and every
		// squadmate would read as stationary forever.
		ACM_TDFriendly record = FindExistingSquadMember(member);
		if (!record)
			record = new ACM_TDFriendly(member, playerId);

		record.entity = member;
		record.playerId = playerId;
		record.distance = distance;

		RefreshFriendly(record, now);

		roster.Insert(record);
	}

	//------------------------------------------------------------------------------------------------
	protected ACM_TDFriendly FindExistingSquadMember(IEntity entity)
	{
		foreach (ACM_TDFriendly member : m_aSquad)
		{
			if (member && member.entity == entity)
				return member;
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	// The wearer's own group, or null.
	//
	// Players resolve through the groups manager. There is deliberately no fallback for a wearer who
	// is not in a group - an empty strip is the correct answer to "where is my squad" when there is
	// no squad, and guessing at one from nearby friendlies would make the panel mean something
	// different depending on circumstances the wearer cannot see.
	//------------------------------------------------------------------------------------------------
	protected SCR_AIGroup ResolveOwnGroup()
	{
		PlayerController controller = GetGame().GetPlayerController();
		if (!controller)
			return null;

		SCR_GroupsManagerComponent groups = SCR_GroupsManagerComponent.GetInstance();
		if (!groups)
			return null;

		return groups.GetPlayerGroup(controller.GetPlayerId());
	}

	//------------------------------------------------------------------------------------------------
	// THREAT BEARING RIBBON
	//
	// Turns the three settled stores into a flat list of "something is over there" marks for the
	// visor's compass strip. Runs once per scan; the per-frame cost of the ribbon is one atan2 per
	// mark and nothing else.
	//
	// The position rule is the SAME rule the markers follow, deliberately re-derived from the same
	// method rather than re-implemented: HasLiveSight() decides whether the live entity may be read,
	// and everything else draws from the frozen position. A ribbon that read GetOrigin()
	// unconditionally would be a wall-tracker with a different shape, and it would be a wall-tracker
	// for contacts the wearer cannot even see a bracket on.
	//
	// Presentation state is deliberately NOT consulted. drawVisible and drawTier answer "did this
	// fit on a crowded screen", and the ribbon exists precisely to carry what did not fit.
	//------------------------------------------------------------------------------------------------
	protected void BuildBearingMarks()
	{
		m_iBearingMarkCount = 0;

		foreach (ACM_TDContact contact : m_aContacts)
		{
			if (!contact)
				continue;

			// A candidate part way through dwell is not a contact yet and does not go on the
			// compass any more than it goes on the display.
			if (contact.ivas && !contact.acquired)
				continue;

			bool live = contact.HasLiveSight();

			vector pos = contact.lastKnownPos;
			if (live)
				pos = contact.entity.GetOrigin();

			if (pos == vector.Zero)
				continue;

			ACM_IvasBearingMark mark = NextBearingMark();
			mark.worldPos = pos;
			mark.frozen = !live;
			mark.shape = ACM_EIvasMarkShape.DOT;

			// Filled means the wearer's own sensor has eyes on it RIGHT NOW. Same question the
			// bracket asks, same answer, from the same method.
			mark.filled = live;

			// A squadmate's report rather than this device's own look. Never filled, and not
			// because of a rule written here - HasLiveSight() refuses delivery != LOCAL, so `live`
			// is already false above for every link contact there will ever be.
			mark.link = (contact.delivery == ACM_TDDelivery.DATALINK);

			// Confidence floored rather than allowed to reach zero. A stale mark should read as
			// faint and old, not vanish - the whole point of keeping it is that the wearer knows
			// something was there.
			mark.packedColor = FadeColor(contact.packedColor, Math.Max(0.4, contact.confidence));

			mark.weight = 2.0;
			mark.height = 1.0;
			mark.glyphRadius = 3.2;

			if (contact.priority == ACM_TDContactPriority.CRITICAL)
			{
				mark.weight = 3.0;
				mark.glyphRadius = 4.2;
			}
			else if (contact.priority == ACM_TDContactPriority.LOW || contact.priority == ACM_TDContactPriority.STALE)
			{
				mark.weight = 1.0;
				mark.height = 0.55;
				mark.glyphRadius = 2.4;
			}
		}

		// Non-combatants, short and thin. They belong on the compass - knowing there are civilians
		// off to your left is exactly the kind of thing this is for - but they must not compete
		// visually with a hostile.
		foreach (ACM_TDNonCombatant record : m_aNonCombatants)
		{
			if (!record || !record.observed)
				continue;

			if (record.lastObservedPos == vector.Zero)
				continue;

			ACM_IvasBearingMark civMark = NextBearingMark();
			civMark.worldPos = record.lastObservedPos;

			// Always broken. A non-combatant record has no live-tracking state at all - the only
			// position it has ever held is an observed one - so solid would be a claim the record
			// cannot make.
			civMark.frozen = true;
			civMark.packedColor = FadeColor(record.packedColor, 0.8);
			civMark.weight = 1.0;
			civMark.height = 0.45;
			civMark.shape = ACM_EIvasMarkShape.SQUARE;
			civMark.glyphRadius = 2.4;

			// Never filled, for the same reason the record has no live-tracking state to consult.
			civMark.filled = false;
			civMark.link = false;
		}

		// Friendlies read live, which is not an exception to the freezing rule but a consequence of
		// it: the rule governs INTELLIGENCE about a target, and a friendly's position is not
		// intelligence. It is known because they are on your side and their own client is sending
		// it. The IFF markers have always drawn them live for the same reason.
		//
		// The ROSTER first and the marker store second. The compass is the one place where "behind
		// you" is the whole point, so a squadmate the marker pass never saw still belongs on it.
		foreach (ACM_TDFriendly squadMate : m_aSquad)
		{
			if (!squadMate || !squadMate.entity)
				continue;

			AddFriendlyBearingMark(squadMate);
		}

		foreach (ACM_TDFriendly friendly : m_aFriendlies)
		{
			if (!friendly || !friendly.entity)
				continue;

			// Already marked as a squadmate. Two marks on one person would read as two people.
			if (FindExistingSquadMember(friendly.entity))
				continue;

			AddFriendlyBearingMark(friendly);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void AddFriendlyBearingMark(ACM_TDFriendly friendly)
	{
		ACM_IvasBearingMark friendMark = NextBearingMark();
		friendMark.worldPos = friendly.entity.GetOrigin();
		friendMark.frozen = false;
		friendMark.packedColor = friendly.packedColor;
		friendMark.weight = 2.0;
		friendMark.height = 0.4;

		// The same diamond AddRoleGlyphCommand puts over their head on the main display.
		friendMark.shape = ACM_EIvasMarkShape.DIAMOND;
		friendMark.glyphRadius = 2.9;
		friendMark.filled = true;
		friendMark.link = false;
	}

	//------------------------------------------------------------------------------------------------
	// Next slot in the pool, growing it only when the busiest scan so far is beaten.
	//------------------------------------------------------------------------------------------------
	protected ACM_IvasBearingMark NextBearingMark()
	{
		ACM_IvasBearingMark mark;

		if (m_iBearingMarkCount < m_aBearingMarks.Count())
		{
			mark = m_aBearingMarks[m_iBearingMarkCount];
		}
		else
		{
			mark = new ACM_IvasBearingMark();
			m_aBearingMarks.Insert(mark);
		}

		m_iBearingMarkCount++;

		return mark;
	}

	// The wearer's own callsign, for the spotter column on a contact this device found itself.
	//
	// The player's NAME rather than the word "you", so the log reads the same whichever machine it
	// is on and a screenshot of it still says who was looking. Falls back to a label rather than to
	// an empty column, so the line never collapses to a bare range.
	//------------------------------------------------------------------------------------------------
	protected string ResolveLocalCallsign()
	{
		PlayerController controller = GetGame().GetPlayerController();
		PlayerManager players = GetGame().GetPlayerManager();

		if (!controller || !players)
			return "OWN SENSOR";

		string name = ACM_PlayerNames.GetDisplayName(controller.GetPlayerId());
		if (name.IsEmpty())
			return "OWN SENSOR";

		name.ToUpper();

		return name;
	}

	//------------------------------------------------------------------------------------------------
	// Who filed a shared report, for the event log line. Falls back to the raw id rather than to
	// nothing, so an unresolvable reporter still reads as a report from somebody.
	//------------------------------------------------------------------------------------------------
	protected string ResolveReporterName(int playerId)
	{
		if (playerId <= 0)
			return "SENSOR";

		PlayerManager players = GetGame().GetPlayerManager();
		if (!players)
			return string.Format("PLAYER %1", playerId);

		string name = ACM_PlayerNames.GetDisplayName(playerId);
		if (name.IsEmpty())
			return string.Format("PLAYER %1", playerId);

		name.ToUpper();

		return name;
	}

	//------------------------------------------------------------------------------------------------
	// One line a second when tracing is on, and it is the only place this system reports on its own
	// load. Counters rather than strings, so they cost nothing to keep updated when it is off.
	//
	// What to read it for: the two trace numbers should never move together - if civilian traces
	// climbing coincides with hostile traces falling, the budgets have leaked into each other.
	// Downgraded and suppressed climbing means the display is at its clutter limit, which is the
	// system working rather than failing, but it is worth knowing before somebody reports that
	// contacts "disappear".
	//------------------------------------------------------------------------------------------------
	protected void LogScanCounters()
	{
		// Split across two calls because string.Format takes nine substitutions and no more. Worth
		// knowing before writing a wide log line: the tenth argument is silently dropped.
		Print(string.Format("[ACM TD] scan: hostile=%1 friendly=%2 civ=%3 unk=%4 neutral=%5 | traces host=%6 nc=%7",
			m_iStatHostiles, m_iStatFriendlies, m_iStatCivilians, m_iStatUnknown, m_iStatNeutral,
			m_iStatHostileTraces, m_iStatNonCombatantTraces), LogLevel.DEBUG);

		Print(string.Format("[ACM TD] draw: full detail=%1 downgraded=%2 suppressed=%3",
			m_iStatFullDetail, m_iStatDowngraded, m_iStatSuppressed), LogLevel.DEBUG);

		Print(string.Format("[ACM TD] store: contacts=%1 friendlies=%2 non-combatants=%3 | filter=%4 detail=%5 iff=%6",
			m_aContacts.Count(), m_aFriendlies.Count(), m_aNonCombatants.Count(),
			SCR_Enum.GetEnumName(ACM_TDContactFilter, m_Provider.GetContactFilter()),
			SCR_Enum.GetEnumName(ACM_TDContactDetail, m_Provider.GetContactDetail()),
			SCR_Enum.GetEnumName(ACM_TDFriendlyIffMode, m_Provider.GetFriendlyIffMode())), LogLevel.DEBUG);
	}

	//------------------------------------------------------------------------------------------------
	// IVAS lifecycle for one contact the trace budget reached this scan.
	//
	// Two outcomes only, and the difference between them is the whole feature: sight held, so
	// read the world and update the stored position - or sight did not, so touch nothing and
	// let what was already stored stand.
	//------------------------------------------------------------------------------------------------
	protected void UpdateIvasState(ACM_TDContact contact, float now)
	{
		if (!contact || !contact.ivas || !contact.traced)
			return;

		if (!contact.hasLineOfSight || !contact.entity)
		{
			BreakSight(contact, now);
			return;
		}

		// Dwell. A run of sight has to be sustained before the contact counts, so a target
		// glimpsed across a doorway does not register.
		if (contact.dwellStart < 0)
			contact.dwellStart = now;

		if (!contact.acquired && (now - contact.dwellStart) >= m_Provider.GetAcquireDwellSeconds())
		{
			contact.acquired = true;
			contact.firstSeenTime = now;

			// Told, not asked. The log is a record of something that has already happened to the
			// contact - it holds no state the contact reads back, so it cannot influence dwell,
			// confidence, expiry or anything else in this method.
			ACM_IvasEventLog.Push(ACM_EIvasEventKind.CONTACT,
				string.Format("CONTACT  %1  %2m", ResolveLocalCallsign(), Math.Round(contact.distance)));

			if (ACM_TD_DEBUG)
				Print(string.Format("[ACM TD] acquired after %1s dwell at %2m", now - contact.dwellStart, contact.distance), LogLevel.DEBUG);
		}

		if (!contact.acquired)
			return;

		// lastKnownPos is deliberately NOT written here. RecordSighting owns it, and owns it
		// alone - see the note in TraceContact.
		contact.lastSeenTime = now;

		// Belt and braces against a future sensor. Only a local visual sighting may put a
		// contact into the live-tracking state; HasLiveSight() enforces the same rule again on
		// the way out, so a mistake here still cannot produce wall tracking.
		contact.locallyConfirmed = (contact.delivery == ACM_TDDelivery.LOCAL && contact.sensor == ACM_TDSensor.VISUAL);
		contact.confidence = 1.0;
	}

	//------------------------------------------------------------------------------------------------
	// Sight lost. Deliberately does NOT write lastKnownPos - the stored value is the last
	// position actually observed and must survive untouched until sight returns.
	//------------------------------------------------------------------------------------------------
	protected void BreakSight(ACM_TDContact contact, float now)
	{
		if (!contact)
			return;

		if (ACM_TD_DEBUG && contact.locallyConfirmed)
			Print(string.Format("[ACM TD] sight lost, freezing at %1", contact.lastKnownPos), LogLevel.DEBUG);

		// Read BEFORE RecordSightingLost, which is what clears locallyConfirmed. Reading it after
		// would make this line unreachable and the log would silently never mention sight breaking.
		if (contact.acquired && contact.locallyConfirmed)
			ACM_IvasEventLog.Push(ACM_EIvasEventKind.LOST, string.Format("SIGHT LOST  %1m  POS HELD", Math.Round(contact.distance)));

		contact.RecordSightingLost();

		contact.confidence = ConfidenceFor(contact, now);
	}

	//------------------------------------------------------------------------------------------------
	// Full confidence through the hold window, then a linear fade. Linear rather than curved
	// because the number drives marker opacity and a curve makes the fade read as a flicker
	// at the shallow end.
	//------------------------------------------------------------------------------------------------
	protected float ConfidenceFor(ACM_TDContact contact, float now)
	{
		if (!contact || contact.lastSeenTime <= 0)
			return 0;

		// Range scaling. A position report ages badly with distance: a target last seen at arm's
		// length is probably still roughly there a few seconds later, while one last seen at the
		// edge of detection range has had time to be anywhere. Both windows shrink together so
		// the shape of the fade is unchanged - it simply runs faster.
		float rangeFactor = RangeFadeFactor(contact);

		float age = now - contact.lastSeenTime;
		float hold = m_Provider.GetConfidenceHoldSeconds() * rangeFactor;
		if (age <= hold)
			return 1.0;

		float decay = m_Provider.GetConfidenceDecaySeconds() * rangeFactor;
		if (decay <= 0)
			return 0;

		float remaining = 1.0 - ((age - hold) / decay);
		return Math.Clamp(remaining, 0, 1);
	}

	//------------------------------------------------------------------------------------------------
	// How much faster this contact goes stale, as a multiplier on the hold and decay windows.
	//
	// Reads contact.distance, which is the range at the last CONFIRMED sighting rather than a
	// live range to the marker. That is the honest number: it is how far away the device was
	// when it learned what it knows, and re-measuring against a position the target may have
	// left would be inventing precision the system does not have.
	//
	// Returns 1.0 when the feature is switched off, so a scaling of zero reproduces the old
	// range-independent behaviour exactly.
	//------------------------------------------------------------------------------------------------
	protected float RangeFadeFactor(ACM_TDContact contact)
	{
		float scaling = m_Provider.GetRangeFadeScaling();
		if (scaling <= 0)
			return 1.0;

		float maxRange = m_Provider.GetMaxRange();
		if (maxRange <= 0)
			return 1.0;

		float ratio = Math.Clamp(contact.distance / maxRange, 0, 1);

		// Floored rather than allowed to reach zero: at scaling 0.9 and maximum range the
		// windows would otherwise collapse far enough that a contact could expire in the same
		// frame it was acquired.
		return Math.Clamp(1.0 - (scaling * ratio), 0.1, 1.0);
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsExpired(ACM_TDContact contact, float now)
	{
		if (!contact)
			return true;

		if (contact.locallyConfirmed)
			return false;

		if (contact.lastSeenTime <= 0)
			return true;

		return (now - contact.lastSeenTime) > m_Provider.GetContactExpireSeconds();
	}

	//------------------------------------------------------------------------------------------------
	// Reuse the previous scan's record for a target so its lastSeenTime survives, which is
	// what makes the memory window work at all.
	//------------------------------------------------------------------------------------------------
	// Dedup lookup, by replicated id first and entity pointer second.
	//
	// The id is what lets a contact survive a change of reporting sensor: a hostile the wearer
	// has been watching and then loses, which a drone is still holding, is ONE contact that
	// changed source - not a local contact plus a drone contact sitting on top of each other.
	// The entity pointer remains the fallback for targets outside replication, where by
	// definition no second sensor can be reporting anyway.
	protected ACM_TDContact FindExistingContact(IEntity entity, int targetId)
	{
		foreach (ACM_TDContact contact : m_aContacts)
		{
			if (!contact)
				continue;

			if (targetId != Replication.INVALID_ID && contact.targetId == targetId)
				return contact;

			if (contact.entity && contact.entity == entity)
				return contact;
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	// DATA LINK INGEST
	//
	// A contact arriving over the network. It has a position, an age and a sensor tag, and that is
	// deliberately all - there is no entity, no replicated id, and no way to obtain either.
	//
	// Delivery is DATALINK unconditionally, which is what makes this safe rather than merely
	// intended: HasLiveSight() requires delivery == LOCAL, so a contact built here can never enter
	// the live-tracking branch of the renderer or reach the outliner, whatever else happens to it
	// later. The sensor tag rides along untouched, because it describes the observation rather than
	// the observer - a hostile spotted visually by a squadmate is sensor=VISUAL here, and still is
	// not something this client can see.
	//------------------------------------------------------------------------------------------------
	void IngestLinkContact(int contactId, vector lastKnownPos, ACM_TDSensor sensor, float age, int reporterPlayerId)
	{
		// Set only on the NEW branch below. The merge case is this client's own contact being
		// refreshed by a squadmate's report, which is not news - it already has a marker on it.
		bool announce = false;

		if (!m_Provider || !m_Provider.ReceivesContacts())
		{
			Print(string.Format("[ACM Link] ingest %1 DROPPED - no active device, or receiving disabled", contactId), LogLevel.DEBUG);
			return;
		}

		float now = System.GetTickCount() / 1000.0;

		ACM_TDContact contact = FindContactByNetworkId(contactId);

		if (!contact)
		{
			// A contact this client has never held. Built with no entity at all - the constructor
			// takes null and the renderer copes, because a frozen contact was always allowed to
			// outlive its entity.
			contact = new ACM_TDContact(null);
			contact.contactId = contactId;
			contact.ivas = true;
			contact.acquired = true;

			// Only hostiles are ever shared, so this is a statement of fact rather than a guess -
			// and it has to be set explicitly because the usual route to affiliation reads the
			// entity, which a link contact does not have.
			contact.affiliation = ACM_TDAffiliation.HOSTILE;
			contact.firstSeenTime = now - age;

			m_aContacts.Insert(contact);

			// Announced further down rather than here. The line carries a range, and the range is
			// not known until the sighting has been recorded and the distance resolved - reporting
			// from this point would print a confident 0m on every shared contact.
			announce = true;
		}

		// Merge case: this client is ALSO looking at the target itself. The binding from its own
		// report told it these are the same enemy, so the shared report refreshes the record it
		// already has rather than adding a second marker beside it.
		//
		// Note the ordering - the local sighting path writes delivery LOCAL every scan it has sight,
		// so this line losing the race is harmless. Whichever writes last, the next local scan
		// restores LOCAL if sight still holds and leaves DATALINK standing if it does not.
		contact.RecordSighting(lastKnownPos, now - age, ACM_TDDelivery.DATALINK, sensor);

		contact.locallyConfirmed = false;
		contact.distance = vector.Distance(GetViewerOrigin(), lastKnownPos);
		contact.confidence = ConfidenceFor(contact, now);

		FinaliseContact(contact, SCR_PlayerController.GetLocalControlledEntity());

		// Same shape as an own-sensor line, so the log reads as one column of "who saw what, how far
		// away" rather than two formats the eye has to switch between. LINK is the only difference,
		// and it is the difference that matters: somebody else saw this, not you.
		if (announce)
		{
			ACM_IvasEventLog.Push(ACM_EIvasEventKind.LINK,
				string.Format("CONTACT  %1  %2m  LINK", ResolveReporterName(reporterPlayerId), Math.Round(contact.distance)));
		}

		Print(string.Format("[ACM Link] ingest %1 OK - store now holds %2 contact(s), dist=%3m confidence=%4 label='%5'",
			contactId, m_aContacts.Count(), Math.Round(contact.distance), contact.confidence, contact.labelText), LogLevel.DEBUG);
	}

	//------------------------------------------------------------------------------------------------
	// The acknowledgement of one of this client's own reports: the server has told it which shared
	// contactId its locally detected target became.
	//
	// This is the ONLY way a client ever learns a binding, and it only ever learns bindings for
	// targets it personally detected - where it already holds the entity, so nothing is revealed.
	//------------------------------------------------------------------------------------------------
	void BindLocalContact(int targetRplId, int contactId)
	{
		foreach (ACM_TDContact contact : m_aContacts)
		{
			if (contact && contact.targetId == targetRplId)
			{
				contact.contactId = contactId;
				return;
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	protected ACM_TDContact FindContactByNetworkId(int contactId)
	{
		if (contactId < 0)
			return null;

		foreach (ACM_TDContact contact : m_aContacts)
		{
			if (contact && contact.contactId == contactId)
				return contact;
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	protected vector GetViewerOrigin()
	{
		IEntity local = SCR_PlayerController.GetLocalControlledEntity();
		if (local)
			return local.GetOrigin();

		return vector.Zero;
	}

	//------------------------------------------------------------------------------------------------
	protected bool PassesLifeStateFilter(IEntity entity)
	{
		if (m_Provider.DetectsIncapacitated())
			return true;

		SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(entity.FindComponent(SCR_CharacterControllerComponent));
		if (!controller)
			return true;

		if (controller.IsDead())
			return false;

		if (controller.GetLifeState() == ECharacterLifeState.INCAPACITATED)
			return false;

		return true;
	}

	//------------------------------------------------------------------------------------------------
	// Candidate gathering. Players always come from PlayerManager because that is exact and
	// costs nothing; AI comes from either a broadphase sphere query or a full agent walk.
	//------------------------------------------------------------------------------------------------
	protected array<IEntity> GatherCandidates(BaseWorld world, IEntity localEntity, vector camPos, float maxRange)
	{
		array<IEntity> result = {};

		PlayerManager playerManager = GetGame().GetPlayerManager();
		int localPlayerId = SCR_PlayerController.GetLocalPlayerId();

		if (playerManager)
		{
			array<int> playerIds = {};
			playerManager.GetPlayers(playerIds);
			foreach (int playerId : playerIds)
			{
				if (playerId == localPlayerId)
					continue;

				IEntity playerEntity = playerManager.GetPlayerControlledEntity(playerId);
				if (playerEntity && playerEntity != localEntity)
					result.Insert(playerEntity);
			}
		}

		if (!m_Provider.DetectsAI())
			return result;

		if (m_Provider.GetScanMode() == ACM_TDScanMode.SPHERE_QUERY && !m_bSphereQueryBroken)
		{
			m_aQueryResults.Clear();
			world.QueryEntitiesBySphere(camPos, maxRange, AddQueryCandidate, FilterQueryCandidate, EQueryEntitiesFlags.ALL);

			foreach (IEntity queried : m_aQueryResults)
			{
				if (result.Find(queried) == -1)
					result.Insert(queried);
			}

			if (!m_aQueryResults.IsEmpty())
			{
				// It works on this build. Stop cross-checking it.
				m_bSphereQueryTrusted = true;
				return result;
			}

			// Empty result from a query we already trust just means nobody is in range,
			// which is the common case and must stay cheap.
			if (m_bSphereQueryTrusted || m_iSphereProbesRemaining <= 0)
				return result;

			m_iSphereProbesRemaining--;

			int countBeforeWalk = result.Count();
			GatherAiAgents(playerManager, result);

			// Only characters the walk found INSIDE our detection radius count as a miss.
			// The agent walk is world-wide, so without the range test any AI anywhere on
			// the map would look like the broadphase had failed.
			int missedNearby = 0;
			for (int i = countBeforeWalk; i < result.Count(); i++)
			{
				if (result[i] && vector.Distance(camPos, result[i].GetOrigin()) <= maxRange)
					missedNearby++;
			}

			if (missedNearby > 0)
			{
				m_iSphereMisses++;
				if (m_iSphereMisses >= SPHERE_MISS_LIMIT)
				{
					m_bSphereQueryBroken = true;
					Print("[ACM TargetDetection] Sphere query is not returning characters on this build - falling back to the AI agent scan for the rest of the session.", LogLevel.WARNING);
				}
			}

			return result;
		}

		GatherAiAgents(playerManager, result);
		return result;
	}

	//------------------------------------------------------------------------------------------------
	protected bool FilterQueryCandidate(IEntity entity)
	{
		// Runs for every entity the broadphase touches, so it has to stay cheap.
		return ChimeraCharacter.Cast(entity) != null;
	}

	//------------------------------------------------------------------------------------------------
	protected bool AddQueryCandidate(IEntity entity)
	{
		if (entity)
			m_aQueryResults.Insert(entity);

		// Returning true keeps the query going.
		return true;
	}

	//------------------------------------------------------------------------------------------------
	// Fallback enumeration, matching the traversal the Shadow Admin ESP uses: top-level
	// agents, descending into groups, skipping agents that already belong to a group so
	// nobody is counted twice, and skipping bodies a player is driving.
	//------------------------------------------------------------------------------------------------
	protected void GatherAiAgents(PlayerManager playerManager, array<IEntity> result)
	{
		AIWorld aiWorld = GetGame().GetAIWorld();
		if (!aiWorld)
			return;

		array<AIAgent> topAgents = {};
		aiWorld.GetAIAgents(topAgents);

		foreach (AIAgent agent : topAgents)
		{
			AIGroup group = AIGroup.Cast(agent);
			if (group)
			{
				array<AIAgent> memberAgents = {};
				group.GetAgents(memberAgents);
				foreach (AIAgent memberAgent : memberAgents)
					CollectAgentEntity(memberAgent, playerManager, result);

				continue;
			}

			if (agent && agent.GetParentGroup())
				continue;

			CollectAgentEntity(agent, playerManager, result);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void CollectAgentEntity(AIAgent agent, PlayerManager playerManager, array<IEntity> result)
	{
		if (!agent)
			return;

		IEntity controlled = agent.GetControlledEntity();
		if (!controlled)
			return;

		if (!ChimeraCharacter.Cast(controlled))
			return;

		// Player-controlled bodies were already added from the player list.
		if (playerManager && playerManager.GetPlayerIdFromControlledEntity(controlled) > 0)
			return;

		if (result.Find(controlled) == -1)
			result.Insert(controlled);
	}

	//------------------------------------------------------------------------------------------------
	// Line of sight, budgeted.
	//
	// Raycasts are by far the most expensive thing here, so only a fixed number run per
	// scan and the starting point rotates. A crowded area therefore degrades in how
	// quickly contacts update, not in frame rate. Contacts not traced this tick keep their
	// previous verdict.
	//------------------------------------------------------------------------------------------------
	protected void ResolveLineOfSight(BaseWorld world, vector camPos, array<ref ACM_TDContact> contacts, float now, float memory)
	{
		if (!m_Provider.RequiresLineOfSight())
		{
			foreach (ACM_TDContact open : contacts)
			{
				open.hasLineOfSight = true;
				open.traced = true;
				open.lastSeenTime = now;
			}

			return;
		}

		int count = contacts.Count();
		if (count == 0)
			return;

		// Kept as plain int arithmetic on purpose. Math.Min/Math.Max return floats, and the
		// cursor below is fed to the modulo operator, which only accepts integers.
		int budget = m_Provider.GetTracesPerScan();
		if (budget > count)
			budget = count;

		IEntity localEntity = SCR_PlayerController.GetLocalControlledEntity();
		int spent = 0;

		// Never-traced contacts go first. They are invisible until their first raycast, so
		// leaving them at the back of a round-robin queue would delay every new contact
		// appearing by however long the backlog takes to clear.
		for (int i = 0; i < count && spent < budget; i++)
		{
			ACM_TDContact fresh = contacts[i];
			if (!fresh || fresh.traced)
				continue;

			TraceContact(world, camPos, fresh, localEntity, now);
			spent++;
		}

		// Whatever budget is left refreshes already-known contacts, round robin, so a
		// crowded area degrades in update latency rather than in frame rate.
		for (int i = 0; i < count && spent < budget; i++)
		{
			int index = (m_iTraceCursor + i) % count;
			ACM_TDContact contact = contacts[index];
			if (!contact || !contact.entity)
				continue;

			TraceContact(world, camPos, contact, localEntity, now);
			spent++;
		}

		// Always advance by at least one, or a scan that traced nothing would leave the
		// cursor parked and keep re-testing the same contacts forever.
		m_iStatHostileTraces = spent;

		int advance = spent;
		if (advance < 1)
			advance = 1;

		m_iTraceCursor = (m_iTraceCursor + advance) % count;
	}

	//------------------------------------------------------------------------------------------------
	protected void TraceContact(BaseWorld world, vector camPos, ACM_TDContact contact, IEntity localEntity, float now)
	{
		if (!contact || !contact.entity)
			return;

		// Aim at centre mass rather than the origin - a trace to the feet is blocked by
		// every low wall and hedge the target happens to be standing behind.
		vector aimPoint = contact.entity.GetOrigin() + Vector(0, TRACE_AIM_HEIGHT_M, 0);

		bool clear = TraceVisibility(world, camPos, aimPoint, localEntity, contact.entity);
		contact.hasLineOfSight = clear;
		contact.traced = true;

		if (!clear)
			return;

		// Filed through the contact's own sensor API rather than written inline, so that this
		// scanner is simply the FIRST sensor rather than the privileged one. A UAV sensor will
		// call the same method with ACM_TDSensor.UAV and get identical freezing, ageing
		// and decay behaviour without touching this file.
		//
		// It happens here, immediately after a successful raycast, because that is the only
		// moment the device has actually confirmed where the target is. Writing it anywhere
		// else - on any tick where a possibly-stale hasLineOfSight happens to be true - would
		// let the stored position creep along behind a wall for as long as the trace budget
		// takes to come back round, and the frozen marker would then be pointing at somewhere
		// the wearer never saw.
		//
		// Legacy contacts get this too. Before the skeleton renderer was removed they were
		// drawn straight off the live entity for the whole memory window, which meant the old
		// system genuinely did track through walls for that period. Now they freeze at the last
		// confirmed position exactly like an IVAS contact.
		contact.RecordSighting(contact.entity.GetOrigin(), now, ACM_TDDelivery.LOCAL, ACM_TDSensor.VISUAL);

		// The local scan is also the reporting sensor. Everything the wearer confirms by raycast is
		// offered to the data link here - the link itself decides whether this device shares, whether
		// the report is inside its rate budget, and whether the server believes it.
		ACM_PlayerLinkComponent link = ACM_PlayerLinkComponent.GetLocal();
		if (link)
			link.OfferSighting(contact, now);
	}

	//------------------------------------------------------------------------------------------------
	// WORLD | ENTS, not DEFAULT.
	//
	// This is the same trap ACM Dive By already hit and documented: DEFAULT does not test
	// entity colliders, and in Reforger buildings, walls and vehicles are all entities. A
	// DEFAULT trace therefore sails straight through a house and reports clear line of
	// sight, which turns the whole system into a wallhack.
	//
	// ANY_CONTACT is the right mode here because this is a yes/no question - we do not care
	// which surface blocked the view, only that something did, so the trace can stop at the
	// first hit instead of resolving the nearest one.
	protected bool TraceVisibility(BaseWorld world, vector from, vector to, IEntity viewer, IEntity target)
	{
		TraceParam param = new TraceParam();
		param.Start = from;
		param.End = to;
		param.Flags = TraceFlags.WORLD | TraceFlags.ENTS | TraceFlags.ANY_CONTACT;
		param.LayerMask = EPhysicsLayerDefs.Default;

		// Excluding the target matters as much as excluding the viewer. Without it the ray
		// hits the target's own collider and every single contact reports as occluded.
		array<IEntity> exclude = {};
		if (viewer)
			exclude.Insert(viewer);
		if (target)
			exclude.Insert(target);
		param.ExcludeArray = exclude;

		// TraceMove returns the fraction of the segment travelled; 1.0 means nothing was
		// hit along the way.
		return world.TraceMove(param, null) >= 0.999;
	}

	//------------------------------------------------------------------------------------------------
	protected void SortByDistance(array<ref ACM_TDContact> contacts)
	{
		// Insertion sort: the list is capped at a couple of dozen entries and is nearly
		// sorted from the previous scan, which is the best case for this.
		for (int i = 1; i < contacts.Count(); i++)
		{
			ACM_TDContact key = contacts[i];
			int j = i - 1;
			while (j >= 0 && contacts[j].distance > key.distance)
			{
				contacts[j + 1] = contacts[j];
				j--;
			}
			contacts[j + 1] = key;
		}
	}

	//------------------------------------------------------------------------------------------------
	// Everything that only needs recomputing at scan rate: IFF, colours, label text and the
	// skeleton lookup.
	//------------------------------------------------------------------------------------------------
	protected void FinaliseContact(ACM_TDContact contact, IEntity localEntity)
	{
		// Remembered contact: colour only. Affiliation is left at whatever it resolved to
		// while the contact was still visible, because re-resolving it would read the entity -
		// and a remembered contact's entity may not exist any more, which is exactly the
		// state one tends to be in once the target has walked out of streaming range.
		if (!contact.HasLiveSight())
		{
			ApplyAffiliationColor(contact);
			contact.labelText = BuildAgeLabel(contact);
			return;
		}

		// Re-resolved every scan rather than trusted from creation: a character can change
		// perceived faction mid-life by changing clothes.
		contact.affiliation = ResolveAffiliation(contact.entity, localEntity);
		ApplyAffiliationColor(contact);
		contact.labelText = BuildLabel(contact);
	}

	//------------------------------------------------------------------------------------------------
	// Age rather than range, for a contact the device is only remembering. Range to a
	// remembered contact is a guess, and printing one would state a distance to something the
	// device can no longer see.
	//
	// Built here, on the scan tick, rather than in the draw pass: the readout changes once a
	// second at most, so rebuilding it per frame would be sixty throwaway strings a second per
	// contact for no visible difference.
	//------------------------------------------------------------------------------------------------
	protected string BuildAgeLabel(ACM_TDContact contact)
	{
		if (!m_Provider.DrawsLabel())
			return string.Empty;

		float age = contact.AgeSeconds(System.GetTickCount() / 1000.0);
		if (age < 0)
			age = 0;

		return string.Format("%1s", Math.Round(age));
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyAffiliationColor(ACM_TDContact contact)
	{
		Color color = m_Provider.GetAffiliationColor(contact.affiliation);
		if (color)
		{
			contact.packedColor = color.PackToInt();
			contact.labelColor = color;
		}
		else
		{
			contact.packedColor = Color.FromRGBA(230, 230, 230, 210).PackToInt();
			contact.labelColor = Color.FromRGBA(230, 230, 230, 255);
		}
	}

	//------------------------------------------------------------------------------------------------
	// Friendly and hostile are separate tests, so a faction that is neither comes out as
	// NEUTRAL rather than being lumped in with the enemy.
	//------------------------------------------------------------------------------------------------
	protected ACM_TDAffiliation ResolveAffiliation(IEntity target, IEntity localEntity)
	{
		// The UsesFactionColors() early return that used to sit here has been removed, and
		// its absence is a bug fix rather than an oversight.
		//
		// That is a DISPLAY setting - "colour every contact the same" - and it was gating a
		// CLASSIFICATION decision. With faction colours switched off, every contact resolved
		// as UNKNOWN, so nothing was ever HOSTILE, so nothing was ever marked as an IVAS
		// contact, so every target fell through to the legacy skeletal renderer. The colour
		// path does not need this guard either: GetAffiliationColor() already returns the
		// unknown colour for everything when the setting is off.
		// SCR_Faction.GetEntityFaction takes a notnull argument, so a null here is a runtime
		// error that unwinds the whole scan rather than a null return. The old early return
		// was accidentally shielding this call; now that it is gone the guard has to be real.
		if (!target || !localEntity)
			return ACM_TDAffiliation.UNKNOWN;

		Faction localFaction = SCR_Faction.GetEntityFaction(localEntity);
		if (!localFaction)
			return ACM_TDAffiliation.UNKNOWN;

		// IFF beacon first. The TRUE faction is what the data link transmits, and a transponder
		// does not care what the wearer has on. A squadmate in captured enemy kit is still a
		// squadmate to the system, which is the whole point of having IFF over a plain optic.
		// Only own-side and allied true factions short-circuit here; everything else still goes
		// through the perceived-faction path below, so an enemy in OUR uniform is still read by
		// the uniform (no beacon) and the disguise mechanic keeps working in that direction.
		Faction trueFaction = SCR_Faction.GetEntityFaction(target);
		if (trueFaction && !IsCivilianFaction(trueFaction.GetFactionKey()))
		{
			if (trueFaction.GetFactionKey() == localFaction.GetFactionKey() || localFaction.IsFactionFriendly(trueFaction))
				return ACM_TDAffiliation.FRIENDLY;
		}

		Faction targetFaction = GetTargetFaction(target);
		if (!targetFaction)
			return ACM_TDAffiliation.UNKNOWN;

		string targetKey = targetFaction.GetFactionKey();

		// Your own side is friendly, full stop, and this test comes first deliberately.
		//
		// It used to go through IsPlayable() on the theory that civilian factions are not playable
		// sides. That was wrong in practice: IsPlayable reflects whether a faction is SELECTABLE BY
		// PLAYERS IN THIS SCENARIO, not whether it is a military side, and in a GM sandbox it reads
		// false for everything - including US. The result was that the wearer's own squad resolved
		// as NEUTRAL and friendly IFF drew nothing at all.
		//
		// It also has to stay ahead of the civilian test below, for the scenario where the wearer IS
		// on a civilian faction. Their own people are their own people.
		if (targetKey == localFaction.GetFactionKey())
			return ACM_TDAffiliation.FRIENDLY;

		// Civilian, and it is its own answer rather than a suppressed friendly.
		//
		// This test used to live INSIDE the IsFactionFriendly branch below, which made "civilian" a
		// subtype of "friendly we happen to hide". That was the wrong shape: Reforger reports
		// civilians as friendly to everyone, so anything that reached the friendly branch first
		// could hand a dockworker a full IFF marker and an inferred tactical role.
		//
		// Lifting it out is the whole classification change in this pass. A civilian can now never
		// be a friendly, whatever the engine says about the faction relationship.
		//
		// An authored key list rather than an inferred property, because the inferred property is
		// exactly what failed. This is boring and it is checkable.
		if (IsCivilianFaction(targetKey))
			return ACM_TDAffiliation.CIVILIAN;

		if (localFaction.IsFactionFriendly(targetFaction))
			return ACM_TDAffiliation.FRIENDLY;

		if (localFaction.IsFactionEnemy(targetFaction))
			return ACM_TDAffiliation.HOSTILE;

		// Resolved, and declared neither. A third military side, not a civilian and not an unknown.
		return ACM_TDAffiliation.NEUTRAL;
	}

	//------------------------------------------------------------------------------------------------
	// Perceived faction accounts for disguises - a character wearing the other side's kit
	// reads as that side. That is the right behaviour for an optical device, which cannot
	// see through a uniform any better than an eye can.
	//------------------------------------------------------------------------------------------------
	// Faction keys that are civilian, whatever the engine says the faction relationship is.
	//
	// Parsed once and re-parsed only when the setting itself changes - this runs per candidate per
	// scan, so splitting a string here every time would be a genuine cost for a value that changes
	// approximately never.
	//------------------------------------------------------------------------------------------------
	protected bool IsCivilianFaction(string factionKey)
	{
		string configured = m_Provider.GetCivilianFactionKeys();

		if (configured != m_sParsedExclusionSource)
		{
			m_sParsedExclusionSource = configured;
			m_aExcludedFactionKeys.Clear();

			array<string> parts = {};
			configured.Split(",", parts, true);

			foreach (string part : parts)
			{
				string trimmed = part;
				trimmed.Trim();
				trimmed.ToUpper();

				if (!trimmed.IsEmpty())
					m_aExcludedFactionKeys.Insert(trimmed);
			}
		}

		if (m_aExcludedFactionKeys.IsEmpty())
			return false;

		string key = factionKey;
		key.ToUpper();

		return m_aExcludedFactionKeys.Find(key) != -1;
	}

	//------------------------------------------------------------------------------------------------
	protected Faction GetTargetFaction(IEntity target)
	{
		if (m_Provider.UsesPerceivedFaction())
		{
			SCR_CharacterFactionAffiliationComponent characterFaction = SCR_CharacterFactionAffiliationComponent.Cast(target.FindComponent(SCR_CharacterFactionAffiliationComponent));
			if (characterFaction && characterFaction.HasPerceivedFaction())
			{
				Faction perceived = characterFaction.GetPerceivedFaction();
				if (perceived)
					return perceived;
			}
		}

		return SCR_Faction.GetEntityFaction(target);
	}

	//------------------------------------------------------------------------------------------------
	protected string BuildLabel(ACM_TDContact contact)
	{
		if (!m_Provider.DrawsLabel())
			return string.Empty;

		// Moved down from the old draw-time marker row, which is gone. Without it the range
		// cap silently stopped applying to labels.
		if (contact.distance > m_Provider.GetLabelMaxRange())
			return string.Empty;

		string label = string.Empty;

		if (m_Provider.LabelShowsName())
		{
			PlayerManager playerManager = GetGame().GetPlayerManager();
			if (playerManager)
			{
				int playerId = playerManager.GetPlayerIdFromControlledEntity(contact.entity);
				if (playerId > 0)
					label = ACM_PlayerNames.GetDisplayName(playerId);
			}
		}

		if (m_Provider.LabelShowsDistance())
		{
			string range = string.Format("%1m", Math.Round(contact.distance));
			if (label.IsEmpty())
				label = range;
			else
				label = label + "  " + range;
		}

		// DETAILED adds where the information came from and how old it is. Only offered on a
		// contact in live sight, because that is the only case where this method runs - a frozen
		// contact gets BuildAgeLabel instead, which is already an age readout.
		if (m_Provider.GetContactDetail() == ACM_TDContactDetail.DETAILED)
		{
			string origin = "VIS";
			if (contact.sensor == ACM_TDSensor.UAV)
				origin = "UAV";
			else if (contact.sensor == ACM_TDSensor.THERMAL)
				origin = "THM";
			else if (contact.sensor == ACM_TDSensor.LASER)
				origin = "LSR";
			else if (contact.sensor == ACM_TDSensor.MANUAL_MARK)
				origin = "MARK";

			if (contact.delivery == ACM_TDDelivery.DATALINK)
				origin = origin + " LINK";

			if (label.IsEmpty())
				label = origin;
			else
				label = label + "  " + origin;
		}

		return label;
	}

	//------------------------------------------------------------------------------------------------
	// Re-run the detection system the way switching IVAS off and on again does.
	//
	// Automates the workaround that was found by hand. Deliberately goes through the same two calls
	// the manual toggle does - Stop() then Activate() - rather than a subset of their effects, so
	// that if this does not work we know the idea was wrong rather than the copy incomplete.
	//------------------------------------------------------------------------------------------------
	//------------------------------------------------------------------------------------------------
	// TEMPORARY DIAGNOSTIC - overlay outage reporter.
	//
	// The overlay going down is easy to see; WHY it went down is not, because five different
	// conditions all end in the same blank screen. This logs the reason on the transition into an
	// outage and then once a second for as long as it lasts, so a permanent outage prints a
	// repeating line naming the exact branch that is holding it down.
	//
	// Remove along with ACM_OVERLAY_DIAG once the cause is known.
	protected static const bool ACM_OVERLAY_DIAG = true;
	protected string m_sOverlayDownReason;
	protected float m_fLastScanTime;

	// Set while a menu holds the overlay down, so the frame the menu closes can be recognised.
	// See the rebuild in DrawTick.
	protected bool m_bMenuHeldDown;
	protected float m_fNextAliveLog;
	protected float m_fNextGateLog;

	// Wall clock of the last completed DrawTick. Read by EnsureTicking to notice that the repeating
	// callqueue entries have stopped firing.
	protected float m_fLastDrawTime;
	protected float m_fNextEnsureLog;
	protected int m_iOverlayBuilds;
	protected float m_fOverlayDownNextLog;

	//------------------------------------------------------------------------------------------------
	// RE-ARM THE TICKS. Called from an ENGINE-DRIVEN update, never from the callqueue.
	//
	// Opening a menu stops the repeating callqueue entries this system runs on - DrawTick and
	// ScanTick both stop firing and do not come back. The 1 Hz watchdog that was supposed to notice
	// could not: it is itself a callqueue entry that re-arms itself through the same queue, so it
	// dies with them. The visible result is a HUD that never returns until the wearer toggles IVAS
	// off and on, because that path runs Start() and re-registers both entries.
	//
	// The static visor overlay appears to survive the same menus only because it is Image and Text
	// widgets: they keep rendering with no tick at all. Its dynamic parts - the NEARBY list - go
	// dark exactly like the contact marks do.
	//
	// So this is driven from ACM_NightVisionComponent.Update, which the gadget system calls every
	// frame regardless of the callqueue. It only acts when the system believes it is running and
	// the draw pass has not fired for a second, and Start() removes both entries before re-adding
	// them, so it cannot stack duplicates. Detection state, contacts and widgets are untouched.
	void EnsureTicking()
	{
		float now = System.GetTickCount() / 1000.0;

		// TEMPORARY - proves whether this is reached at all.
		//
		// Printed BEFORE every guard and rate limited to once a second. Its silence after a menu
		// previously could have meant either "the engine update that calls this stopped" or "it ran
		// and returned at a guard", and those need opposite fixes. This separates them: if these
		// lines appear after a menu, the engine-driven update is alive and the guard values say why
		// nothing was re-armed. If they stop, the gadget update is dead too and the tick source has
		// to come from somewhere else entirely.
		if (ACM_OVERLAY_DIAG && now >= m_fNextEnsureLog)
		{
			m_fNextEnsureLog = now + 1.0;

			string e = "[ACM IVAS] EnsureTicking run=";
			e += m_bRunning.ToString();
			e += " prov=";
			e += (m_Provider != null).ToString();
			e += " sinceDraw=";
			e += (now - m_fLastDrawTime).ToString();

			Print(e, LogLevel.DEBUG);
		}

		if (!m_bRunning || !m_Provider)
			return;

		// One second is comfortably longer than a frame and shorter than a player notices.
		if (now - m_fLastDrawTime < 1.0)
			return;

		if (ACM_OVERLAY_DIAG)
			Print("[ACM IVAS] draw ticks stalled - re-arming from engine update", LogLevel.DEBUG);

		Start();
	}

	//------------------------------------------------------------------------------------------------
	// The buffer currently being built into / submitted.
	protected array<ref CanvasWidgetCommand> CurrentDrawBuffer()
	{
		if (m_bDrawBufferB)
			return m_aDrawCommandsB;

		return m_aDrawCommandsA;
	}

	//------------------------------------------------------------------------------------------------
	// Move to the other buffer and hand it back cleared, ready to be built into.
	protected array<ref CanvasWidgetCommand> FlipDrawBuffer()
	{
		m_bDrawBufferB = !m_bDrawBufferB;

		array<ref CanvasWidgetCommand> buffer = CurrentDrawBuffer();
		buffer.Clear();

		return buffer;
	}

	//------------------------------------------------------------------------------------------------
	// TEMPORARY - a big magenta X plus a box through the middle of the screen, in raw screen
	// coordinates. No contact, position, projection, faction or visibility logic is involved.
	//------------------------------------------------------------------------------------------------
	protected void AppendCanvasProbe(array<ref CanvasWidgetCommand> buffer, WorkspaceWidget workspace)
	{
		if (!buffer || !workspace)
			return;

		float cx = workspace.GetWidth() * 0.5;
		float cy = workspace.GetHeight() * 0.5;
		float r = 300;

		// Same construction the contact colours use, rather than a helper this file never calls.
		int color = Color.FromRGBA(255, 0, 255, 255).PackToInt();
		float width = 10;

		// X
		AppendProbeLine(buffer, cx - r, cy - r, cx + r, cy + r, color, width);
		AppendProbeLine(buffer, cx - r, cy + r, cx + r, cy - r, color, width);

		// box
		AppendProbeLine(buffer, cx - r, cy - r, cx + r, cy - r, color, width);
		AppendProbeLine(buffer, cx + r, cy - r, cx + r, cy + r, color, width);
		AppendProbeLine(buffer, cx + r, cy + r, cx - r, cy + r, color, width);
		AppendProbeLine(buffer, cx - r, cy + r, cx - r, cy - r, color, width);
	}

	//------------------------------------------------------------------------------------------------
	protected void AppendProbeLine(array<ref CanvasWidgetCommand> buffer, float x0, float y0, float x1, float y1, int color, float width)
	{
		LineDrawCommand line = new LineDrawCommand();
		line.m_Vertices = { x0, y0, x1, y1 };
		line.m_iColor = color;
		line.m_fWidth = width;

		buffer.Insert(line);
	}

	//------------------------------------------------------------------------------------------------
	// Defensive canvas invalidation for the menu-close case.
	//
	// Submits a cleared alternate buffer once, so the canvas holds a different, empty list the
	// moment the overlay goes live again. The next ordinary pass flips once more and rebuilds the
	// full contact set, so at worst one frame is empty - invisible next to a HUD that never returns.
	//
	// No widget is created, destroyed, hidden or shown here, and no detection state is touched.
	protected void InvalidateCanvasBuffers()
	{
		array<ref CanvasWidgetCommand> buffer = FlipDrawBuffer();
		m_aActiveCommands = buffer;

		if (m_wCanvas)
			m_wCanvas.SetDrawCommands(buffer);
	}

	//------------------------------------------------------------------------------------------------
	// Draw pass - runs every frame
	//------------------------------------------------------------------------------------------------
	protected void DrawTick()
	{
		m_fLastDrawTime = System.GetTickCount() / 1000.0;

		// TEMPORARY - gate heartbeat, printed before any early return.
		//
		// The detailed line at the end of this function only prints when the whole pass completes,
		// so a pass that returns early is silent and indistinguishable from a pass that never ran.
		// This prints every gate's value once a second from the very top: if it appears after a
		// menu closes but the detailed line does not, the false value here names the gate that is
		// holding the overlay down. If it does NOT appear at all, DrawTick itself has stopped.
		if (ACM_OVERLAY_DIAG)
		{
			float gateNow = System.GetTickCount() / 1000.0;
			if (gateNow >= m_fNextGateLog)
			{
				m_fNextGateLog = gateNow + 1.0;

				bool gMenu = false;
				MenuManager gMenus = GetGame().GetMenuManager();
				if (gMenus)
					gMenu = gMenus.IsAnyMenuOpen();

				bool gIvas = false;
				bool gHud = false;
				if (m_Provider)
				{
					gIvas = m_Provider.IsIvasEnabled();
					gHud = m_Provider.IsHudEnabled();
				}

				string g = "[ACM IVAS] tick run=";
				g += m_bRunning.ToString();
				g += " prov=";
				g += (m_Provider != null).ToString();
				g += " ws=";
				g += (GetGame().GetWorkspace() != null).ToString();
				g += " menu=";
				g += gMenu.ToString();
				g += " ivas=";
				g += gIvas.ToString();
				g += " hud=";
				g += gHud.ToString();
				g += " root=";
				g += (m_wRoot != null).ToString();
				g += " canvas=";
				g += (m_wCanvas != null).ToString();

				Print(g, LogLevel.DEBUG);
			}
		}

		if (!m_bRunning || !m_Provider)
		{
			ClearOverlay();
			return;
		}

		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if (!workspace)
			return;

		// A menu being open is a teardown case, not a skip case: the canvas would otherwise
		// keep rendering the last command list on top of the open menu.
		//
		// m_bMenuHeldDown records that a menu held the overlay down, so the frame it closes can
		// invalidate the canvas draw state - see the transition below.
		MenuManager menus = GetGame().GetMenuManager();
		if (menus && menus.IsAnyMenuOpen())
		{
			m_bMenuHeldDown = true;

			ClearOverlay();
			return;
		}

		// IVAS master off. Everything augmented goes down together - markers of every
		// classification and data link presentation. Night vision and thermal are untouched:
		// they are separate hardware with their own switches, and a master that killed them
		// would make this a "turn the goggles off" button by accident.
		//
		// The remote feed is stopped by the setter rather than here, because stopping it means
		// releasing a camera slot rather than skipping a draw.
		if (!m_Provider.IsIvasEnabled() || !m_Provider.IsHudEnabled())
		{
			ClearOverlay();
			return;
		}

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return;

		// A MENU HAS JUST CLOSED - INVALIDATE THE CANVAS DRAW STATE, NOTHING ELSE.
		//
		// Dead ends, all tried and all reverted: rebuilding the overlay widget (a root created by
		// CreateWidgets after the menu system has cycled its layers does not render at all),
		// restarting the draw/scan timers, and cycling detection off and on the way the manual
		// IVAS toggle does. None of them was the fault, and each left the symptom untouched or
		// worse. The static visor overlay - Image and Text widgets, no canvas - recovers from the
		// same menus on its own, which is what points at the draw-command path rather than at
		// widget lifetime or detection state.
		if (m_bMenuHeldDown)
		{
			m_bMenuHeldDown = false;

			// The canvas may still hold the cleared list from while the menu was up, and handing it
			// that same instance again would not tell it anything changed. One cleared alternate
			// buffer here; the next ordinary pass rebuilds the full contact set into the other one.
			//
			// Nothing else happens on this transition by design - no widget rebuild, no detection
			// restart, no timer restart, no contact reset. All were tried; none was the fault.
			InvalidateCanvasBuffers();

			// Arm the capability probe for three seconds. It bypasses contacts, acquisition,
			// factions, LOS, classification, projection and the detection system entirely - it is
			// four hardcoded lines in screen space, submitted through the same canvas reference the
			// markers use.
			// PROBE OFF.
			//
			// Nothing may paint over this transition while the real fix is being judged. The
			// marks either come back on their own now or they do not.
			m_fCanvasProbeUntil = 0;
		}

		EnsureOverlay();
		if (!m_wRoot)
		{
			return;
		}

		// Reaching here means the overlay is drawing. Reset the reporter so the NEXT outage logs
		// its reason immediately instead of being suppressed as a repeat.

		// Re-point projection at the scope camera when looking through a magnified optic.
		UpdateScopeProjection();

		// Flip first, then build into the buffer about to be submitted. The canvas keeps rendering
		// the previous buffer until SetDrawCommands below hands it this one.
		array<ref CanvasWidgetCommand> frameBuffer = FlipDrawBuffer();
		m_aActiveCommands = frameBuffer;

		m_iDiagEntering = 0;
		m_iDiagProjected = 0;
		m_iDiagDrawable = 0;
		m_bDiagHaveFirst = false;

		float now = System.GetTickCount() / 1000.0;

		m_bTraceFriendlyDraw = false;
		if (ACM_TD_DEBUG && now >= m_fNextFriendlyDrawLogTime)
		{
			m_bTraceFriendlyDraw = true;
			m_fNextFriendlyDrawLogTime = now + 1.0;
			Print(string.Format("[ACM IFF] ---- draw pass: %1 friendly, %2 contact ----", m_aFriendlies.Count(), m_aContacts.Count()), LogLevel.DEBUG);
		}

		int labelIndex = 0;

		// DRAW ORDER IS LABEL PRIORITY.
		//
		// Labels come from one finite pool shared by every pass, and the overflow simply draws no
		// text - so whichever pass runs last is the one that loses its labels in a crowded scene.
		// That makes this ordering a real decision rather than a stylistic one.
		//
		// Ranked so the things that get somebody killed if they go silent are served first: the
		// hostile in front of you, then your own squad, then the ordinary contact, then the
		// civilians and unknowns, then the fading marks. A civilian glyph with no text is still a
		// correct civilian marker; a locally confirmed hostile that lost its range readout is not.
		labelIndex = DrawRankedContacts(ACM_TDContactPriority.CRITICAL, ACM_TDContactPriority.HIGH, workspace, world, now, labelIndex);

		foreach (ACM_TDFriendly friendly : m_aFriendlies)
			labelIndex = DrawFriendly(friendly, workspace, world, labelIndex);

		labelIndex = DrawRankedContacts(ACM_TDContactPriority.NORMAL, ACM_TDContactPriority.NORMAL, workspace, world, now, labelIndex);

		foreach (ACM_TDNonCombatant record : m_aNonCombatants)
			labelIndex = DrawNonCombatant(record, workspace, world, now, labelIndex);

		labelIndex = DrawRankedContacts(ACM_TDContactPriority.LOW, ACM_TDContactPriority.STALE, workspace, world, now, labelIndex);

		for (int i = labelIndex; i < m_aLabels.Count(); i++)
		{
			if (m_aLabels[i])
				m_aLabels[i].SetVisible(false);
		}

		// KEEPALIVE COMMAND - the actual fix, and the reason for it is worth the paragraph.
		//
		// The probe established this empirically: after a menu closes, a buffer containing ONLY
		// marker commands is submitted to a valid canvas, at valid on-screen coordinates, and
		// renders nothing - while the identical buffer with six extra hardcoded lines appended
		// renders everything, markers included. Marks return the moment the probe is present and
		// vanish the moment it expires, with every counter unchanged either side.
		//
		// So one deliberately invisible command rides along in every buffer. It is a two-pixel
		// line in the top-left corner at alpha 1, which costs nothing and cannot be seen, and it
		// keeps whatever the canvas needs in order to present the list from going absent.
		//
		// This is a workaround, not an explanation: the underlying reason a marker-only buffer
		// stops presenting after a menu is still unknown. It is documented here rather than hidden
		// so that whoever finds the real cause can delete this with confidence.
		int keepAliveColor = Color.FromRGBA(0, 0, 0, 1).PackToInt();
		AppendProbeLine(frameBuffer, 0, 0, 2, 0, keepAliveColor, 1);

		// TEMPORARY - the visible probe, still armed for three seconds after a menu closes. Kept
		// only until the keepalive above is confirmed: if markers now persist after the magenta X
		// disappears, the keepalive works and both this and the probe can go.
		if (ACM_OVERLAY_DIAG && now < m_fCanvasProbeUntil)
			AppendCanvasProbe(frameBuffer, workspace);

		if (m_wCanvas)
			m_wCanvas.SetDrawCommands(frameBuffer);

		// TEMPORARY DIAGNOSTIC - one line a second. Answers whether, after a menu closes: detection
		// still runs, contacts still exist, the canvas still exists, commands are still generated,
		// and the buffer object is actually alternating.
		if (ACM_OVERLAY_DIAG && now >= m_fNextAliveLog)
		{
			m_fNextAliveLog = now + 1.0;

			string canvasState = "null";
			if (m_wCanvas)
				canvasState = "valid";

			string bufferId = "A";
			if (m_bDrawBufferB)
				bufferId = "B";

			string probeState = "off";
			if (now < m_fCanvasProbeUntil)
				probeState = "ON";

			// firstMark is where the first drawable mark was actually placed, against the screen
			// size. The probe proved the canvas can draw; if these coordinates leave the screen
			// after a menu then the marks are being drawn correctly into empty space and the fault
			// is the projection, not the canvas and not the widget.
			// Built one statement at a time on purpose: Enforce Script rejects a long concatenation
			// chain with "Formula too complex", so each field is appended separately.
			string line = "[ACM IVAS] scan[cand=";
			line += m_iDiagCandidates.ToString();
			line += " fr=";
			line += m_iDiagFriendlyScan.ToString();
			line += " cam=";
			line += m_vDiagCamPos.ToString();
			line += "]  draw[enter=";
			line += m_iDiagEntering.ToString();
			line += " proj=";
			line += m_iDiagProjected.ToString();
			line += " drawable=";
			line += m_iDiagDrawable.ToString();
			line += " cmds=";
			line += frameBuffer.Count().ToString();
			line += "]  canvas=";
			line += canvasState;
			line += " firstMark=(";
			line += Math.Round(m_fDiagFirstX).ToString();
			line += ",";
			line += Math.Round(m_fDiagFirstY).ToString();
			line += ") mask=";
			line += m_fDiagFirstMask.ToString();
			line += " screen=";
			line += workspace.GetWidth().ToString();
			line += "x";
			line += workspace.GetHeight().ToString();
			line += " probe=";
			line += probeState;

			Print(line, LogLevel.DEBUG);
		}

		DrawFeedPass(workspace, world, now);
	}

	//------------------------------------------------------------------------------------------------
	// The same markers again, through the remote feed's camera.
	//
	// Outlines are not re-done here and do not need to be: OutlineEntity works on a world entity,
	// so any camera looking at an outlined target already sees the silhouette. What is missing from
	// the feed is everything screen-space, because it was projected through the main camera.
	//
	// Labels are skipped by handing the pass an already-exhausted label index. The pool belongs to
	// the main overlay and its widgets cannot be parented into the feed panel, so text needs its
	// own pool in the feed layout before it can work - deliberately left for later rather than
	// half-done here.
	//------------------------------------------------------------------------------------------------
	protected void DrawFeedPass(WorkspaceWidget workspace, BaseWorld world, float now)
	{
		ACM_RemoteFeedRenderer feed = ACM_RemoteFeedRenderer.GetInstance();
		if (!feed || !feed.IsRenderingFeed())
			return;

		CanvasWidget feedCanvas = feed.GetFeedCanvas();
		if (!feedCanvas)
			return;

		// The scope clip belongs to the main view only. The feed is its own camera looking at its
		// own surface, and testing its marks against the weapon optic's glass would blank the feed
		// whenever the wearer happened to be aiming down a scope. UpdateScopeProjection recomputes
		// this at the top of the next draw pass, so clearing it here needs no restore.
		m_bScopeProj = false;
		m_ScopeSights = null;

		// Origin stays at zero: these are canvas-local coordinates, not screen ones.
		m_fPanelX = 0;
		m_fPanelY = 0;

		if (!feed.GetFeedSurfaceSize(m_fPanelW, m_fPanelH))
			return;

		vector camMat[4];
		if (!feed.GetFeedCameraTransform(camMat))
			return;

		// Point the alias at the feed's buffer, so none of the drawing code has to know there are
		// now two surfaces. Ownership does not move.
		m_aActiveCommands = m_aFeedDrawCommands;
		m_aFeedDrawCommands.Clear();

		m_iProjCamera = feed.GetCamSlot();
		m_bProjToPanel = true;

		int drawn = AcquireAndDrawFeedTargets(workspace, world, camMat);

		feedCanvas.SetDrawCommands(m_aFeedDrawCommands);

		if (ACM_TD_DEBUG && m_bTraceFriendlyDraw)
			Print(string.Format("[ACM IFF] feed pass: %1 target(s), %2 command(s) onto a %3x%4 surface", drawn, m_aFeedDrawCommands.Count(), m_fPanelW, m_fPanelH), LogLevel.DEBUG);

		m_iProjCamera = -1;
		m_bProjToPanel = false;
		m_aActiveCommands = CurrentDrawBuffer();
	}

	//------------------------------------------------------------------------------------------------
	// The feed's own acquisition pass.
	//
	// This does NOT reuse the wearer's contact list, and that is the point. Those contacts were
	// acquired from the wearer's position, through the wearer's cone, against the wearer's line of
	// sight - re-projecting them into a camera that is somewhere else entirely would mark targets
	// the camera cannot see and miss everything it can.
	//
	// It is also deliberately stateless: no dwell, no memory window, no contact store. Every pass
	// starts from nothing and draws only what the camera can see RIGHT NOW. A remembered contact
	// belongs to the sensor that remembered it, and letting a drone feed write into the wearer's
	// store would quietly turn a camera into a second pair of eyes with the wearer's persistence.
	//
	// The security properties come from reusing the same helpers the main scan uses, not from
	// reimplementing them: the same candidate gathering, the same life-state filter, the same
	// affiliation resolution, and above all the same WORLD|ENTS trace. A cheaper trace here would
	// be a wallhack that only shows up on a drone.
	//------------------------------------------------------------------------------------------------
	protected int AcquireAndDrawFeedTargets(WorkspaceWidget workspace, BaseWorld world, vector camMat[4])
	{
		IEntity localEntity = SCR_PlayerController.GetLocalControlledEntity();
		if (!localEntity)
			return 0;

		vector camPos = camMat[3];
		vector camForward = camMat[2];

		float maxRange = m_Provider.GetMaxRange();
		float halfFovCos = Math.Cos((m_Provider.GetFovDegrees() * 0.5) * Math.DEG2RAD);

		array<IEntity> candidates = GatherCandidates(world, localEntity, camPos, maxRange);

		int drawn = 0;
		int budget = m_Provider.GetMaxTargets();

		foreach (IEntity candidate : candidates)
		{
			if (drawn >= budget)
				break;

			if (!candidate)
				continue;

			vector targetPos = candidate.GetOrigin();
			float distance = vector.Distance(camPos, targetPos);
			if (distance > maxRange)
				continue;

			// FOV cone. Cheap, so it runs before any raycast.
			vector toTarget = targetPos - camPos;
			float length = toTarget.Length();
			if (length > 0.01)
			{
				vector direction = toTarget / length;
				if (vector.Dot(direction, camForward) < halfFovCos)
					continue;
			}

			if (!PassesLifeStateFilter(candidate))
				continue;

			// Line of sight from the CAMERA, not from the wearer. Chest height on both ends, the
			// same as the main scan, so a target crouched behind a wall is occluded rather than
			// caught by a ray that happened to clear it.
			vector eye = camPos;
			vector aim = targetPos + Vector(0, TRACE_AIM_HEIGHT_M, 0);
			if (m_Provider.RequiresLineOfSight() && !TraceVisibility(world, eye, aim, null, candidate))
				continue;

			ACM_TDAffiliation affiliation = ResolveAffiliation(candidate, localEntity);

			// Same ladder, same switches, same filter as the main view. The feed is a different
			// camera, not a different device - a civilian must read as a civilian in the picture
			// too, or the wearer learns to trust one surface and not the other.
			if (!PassesFeedFilter(affiliation, distance))
				continue;

			Color color = m_Provider.GetAffiliationColor(affiliation);
			if (!color)
				continue;

			if (DrawFeedTarget(workspace, world, targetPos, affiliation, color))
				drawn++;
		}

		return drawn;
	}

	//------------------------------------------------------------------------------------------------
	// What the feed is allowed to mark.
	//
	// Deliberately re-derived from the wearer's settings rather than shared with the main pass:
	// this acquisition is stateless and camera-local, so there is no per-target record to carry a
	// decision on. The settings are the same; the geometry is not.
	//------------------------------------------------------------------------------------------------
	protected bool PassesFeedFilter(ACM_TDAffiliation affiliation, float distance)
	{
		if (!ACM_IffServerSettings.Allows(ACM_IffServerSettings.MASTER))
			return false;

		if (affiliation == ACM_TDAffiliation.FRIENDLY)
			return m_Provider.GetFriendlyIffMode() != ACM_TDFriendlyIffMode.OFF;

		if (affiliation == ACM_TDAffiliation.HOSTILE)
			return m_Provider.IsHostileTrackingEnabled();

		ACM_TDContactFilter filter = m_Provider.GetContactFilter();

		if (affiliation == ACM_TDAffiliation.CIVILIAN)
			return m_Provider.IsCivilianIffEnabled() && filter != ACM_TDContactFilter.THREATS && distance <= m_Provider.GetCivilianMaxRange();

		if (affiliation == ACM_TDAffiliation.UNKNOWN)
			return m_Provider.ShowsUnknownContacts() && filter != ACM_TDContactFilter.THREATS && distance <= m_Provider.GetCivilianMaxRange();

		return filter == ACM_TDContactFilter.ALL && distance <= m_Provider.GetCivilianMaxRange();
	}

	//------------------------------------------------------------------------------------------------
	// One body box, in the affiliation colour. The outliner cannot draw into a render-target
	// camera, so inside the feed the box carries the identification on its own.
	//------------------------------------------------------------------------------------------------
	protected bool DrawFeedTarget(WorkspaceWidget workspace, BaseWorld world, vector groundPos, ACM_TDAffiliation affiliation, Color color)
	{
		float centerX, topY, botY, halfWidth;
		if (!ProjectBodyBox(groundPos, workspace, world, centerX, topY, botY, halfWidth))
			return false;

		if (MaskFactor(workspace, centerX, (topY + botY) * 0.5) <= 0)
			return false;

		// Same packing the rest of the system uses. Hand-rolling an ARGB here would be a second
		// way of doing one thing, and the two would drift.
		int packed = color.PackToInt();

		// Same visual vocabulary as the main view, for the same reason it exists there: shape
		// carries the classification. A civilian marked with acquisition brackets in the drone
		// picture is exactly the mistake this pass is meant to prevent, and getting it right on one
		// surface and wrong on the other is worse than getting it wrong on both.
		if (affiliation == ACM_TDAffiliation.CIVILIAN || affiliation == ACM_TDAffiliation.UNKNOWN || affiliation == ACM_TDAffiliation.FRIENDLY)
		{
			float glyphSize = (botY - topY) * 0.18;
			if (glyphSize < 4)
				glyphSize = 4;

			AddRoleGlyphCommand(centerX, (topY + botY) * 0.5, glyphSize, packed);
			return true;
		}

		if (affiliation == ACM_TDAffiliation.NEUTRAL)
		{
			float squareSize = (botY - topY) * 0.16;
			if (squareSize < 4)
				squareSize = 4;

			AddSquareGlyphCommand(centerX, (topY + botY) * 0.5, squareSize, packed);
			return true;
		}

		// Hostiles keep the corner brackets. There is still no silhouette in here - the engine
		// outliner takes no camera parameter and does not reach a render-target camera - so the
		// bracket carries the identification on its own.
		AddCornerBoxCommand(centerX, topY, botY, halfWidth, packed);

		return true;
	}

	//------------------------------------------------------------------------------------------------
	// Instrumentation. Gated behind ACM_TD_DEBUG, off in production.
	//
	// The whole point of these three is to make a claim about the render path falsifiable from
	// a log file instead of from reading the source. Every contact drawn reports which of the
	// two renderers ran and how much legacy geometry work happened while it ran, so an IVAS
	// contact that quietly falls through to the skeleton is impossible to miss.
	//------------------------------------------------------------------------------------------------
	protected string DebugContactId(ACM_TDContact contact)
	{
		if (!contact)
			return "<null>";

		// Frozen contacts intentionally keep no live handle, so this reads "<frozen>" rather
		// than crashing - and that is itself information: a frozen contact must never be able
		// to reach a renderer that needs an entity.
		if (!contact.entity)
			return "<frozen>";

		return contact.entity.Type().ToString();
	}

	//------------------------------------------------------------------------------------------------
	protected string DebugAffiliationName(ACM_TDAffiliation affiliation)
	{
		switch (affiliation)
		{
			case ACM_TDAffiliation.FRIENDLY: return "FRIENDLY";
			case ACM_TDAffiliation.HOSTILE:  return "HOSTILE";
			case ACM_TDAffiliation.NEUTRAL:  return "NEUTRAL";
		}

		return "UNKNOWN";
	}

	//------------------------------------------------------------------------------------------------
	protected string DebugFactionKey(Faction faction)
	{
		if (!faction)
			return "<unresolved>";

		return faction.GetFactionKey();
	}

	//------------------------------------------------------------------------------------------------
	// The faction key is the whole answer when nothing is being marked as IVAS: affiliation is
	// derived from it, so seeing the two keys side by side says immediately whether the sensor
	// is misclassifying or whether the test simply has no enemies in it.
	//------------------------------------------------------------------------------------------------
	protected void LogClassification(ACM_TDContact contact, Faction targetFaction)
	{
		if (!contact)
			return;

		Print(string.Format("[ACM TD] classify id=%1 faction=%2 affiliation=%3 ivas=%4 dist=%5m acquired=%6 confirmed=%7",
			DebugContactId(contact),
			DebugFactionKey(targetFaction),
			DebugAffiliationName(contact.affiliation),
			contact.ivas,
			contact.distance,
			contact.acquired,
			contact.locallyConfirmed), LogLevel.DEBUG);
	}

	//------------------------------------------------------------------------------------------------
	// Contact renderer
	//
	// One renderer for every contact, in the same visual language as the Shadow Admin ESP: a
	// projected body box with a label above it. No skeleton, no hull, no bone reads anywhere in
	// this file any more. A box needs two points - the ground the contact is standing on, and a
	// fixed body height above it - and that is the entirety of the geometry.
	//
	// The rule that governs everything below:
	//
	//   LINE OF SIGHT controls the entity outline. The engine outliner renders through walls by
	//   design, so it may only ever be pointed at an entity the wearer can genuinely see this
	//   instant. A contact the device merely REMEMBERS - and, once the data link lands, a
	//   contact another wearer is sharing - gets a marker at a stored position and no outline,
	//   no matter how confident the device is about it.
	//
	// So the branch below is not cosmetic. Confirmed reads the live entity because sight is
	// established. Remembered reads contact.lastKnownPos and touches nothing else.
	//
	// The two states are also visually exclusive, which falls out of the same rule. While
	// sight holds, the silhouette marks the target and no box is drawn. The moment sight
	// breaks there is no longer an entity the outliner is permitted to touch, and the box
	// appears at the stored position in its place. So the wearer never sees both at once, and
	// which one they are looking at tells them whether the device can still see the target or
	// is only remembering it.
	//------------------------------------------------------------------------------------------------
	// One tier band, in ranked order. Split out so the draw order in DrawTick can interleave the
	// hostile tiers with the other passes and get the label priority right.
	//------------------------------------------------------------------------------------------------
	protected int DrawRankedContacts(ACM_TDContactPriority from, ACM_TDContactPriority to, WorkspaceWidget workspace, BaseWorld world, float now, int labelIndex)
	{
		foreach (ACM_TDContact contact : m_aRankedContacts)
		{
			if (!contact || !contact.drawVisible)
				continue;

			// Compared as plain ints. EnforceScript will happily order enums, but spelling the
			// conversion out here keeps the intent obvious: the enum is declared in presentation
			// order, and that ordering is load-bearing.
			int tier = contact.drawTier;
			int lowest = from;
			int highest = to;

			if (tier < lowest || tier > highest)
				continue;

			labelIndex = DrawContact(contact, workspace, world, now, labelIndex);
		}

		return labelIndex;
	}

	//------------------------------------------------------------------------------------------------
	protected int DrawContact(ACM_TDContact contact, WorkspaceWidget workspace, BaseWorld world, float now, int labelIndex)
	{
		if (!m_Provider.IsHostileTrackingEnabled())
			return labelIndex;

		// In the store purely so its dwell timer survives the scan - see the keep pass. It is
		// a candidate, not a contact, and a candidate draws nothing.
		if (contact.ivas && !contact.acquired)
			return labelIndex;

		// Budgeted off the display this scan. The contact itself is completely untouched - it is
		// still ageing, still deduping and still holding its network identity.
		if (!contact.drawVisible)
			return labelIndex;

		bool confirmed = contact.HasLiveSight();

		vector groundPos;
		float confidence = 1.0;

		if (confirmed)
		{
			groundPos = contact.entity.GetOrigin();
		}
		else
		{
			groundPos = contact.lastKnownPos;

			// Legacy contacts have no confidence model - they live on the memory window and
			// ResolveLineOfSight drops them when it runs out - so they hold full alpha for
			// however long they exist.
			if (contact.ivas)
			{
				confidence = ConfidenceFor(contact, now);
				if (confidence <= 0)
					return labelIndex;
			}
		}

		float centerX, topY, botY, halfWidth;
		if (!ProjectBodyBox(groundPos, workspace, world, centerX, topY, botY, halfWidth))
			return labelIndex;

		// Display masking. A marker is only meaningful where the wearer can actually see it, so
		// anything projecting outside the usable region is dropped here - before it becomes a
		// draw command, rather than being drawn and clipped. An off-display contact costs one
		// projection and nothing else.
		float maskFactor = MaskFactor(workspace, centerX, (topY + botY) * 0.5);
		if (maskFactor <= 0)
			return labelIndex;

		// One number drives every visual: distance-accelerated confidence for how stale the
		// information is, times the mask for how close to the edge of the display it sits.
		float visibility = confidence * maskFactor;

		int color = contact.packedColor;
		if (visibility < 1.0)
			color = FadeColor(color, visibility);

		// STALE: a mark that something was here, and nothing else. No brackets, no text.
		//
		// Distinct in shape rather than merely dimmer, because a faded bracket at the edge of
		// attention reads as a live contact seen badly, and this is the opposite of that - a
		// position the device is no longer confident about at all.
		if (contact.drawTier == ACM_TDContactPriority.STALE)
		{
			AddStaleMarkCommand(centerX, (topY + botY) * 0.5, halfWidth, color);
			return labelIndex;
		}

		// While sight holds, the silhouette traces the target and a rectangle around it is just a
		// second, cruder outline of the same thing - so the box is for when there is nothing left
		// to outline.
		//
		// With silhouettes switched off, though, there IS nothing left, and that case now falls
		// through to the box instead of drawing nothing at all. It matters more than it did:
		// silhouettes are a radial toggle now rather than a prefab setting, so a player can turn
		// them off mid-mission and would otherwise lose their confirmed contacts entirely.
		bool outlined = false;
		if (confirmed && m_Provider.OutlinesConfirmedContacts())
			outlined = DrawEntityOutline(contact, maskFactor);

		if (!outlined)
			AddCornerBoxCommand(centerX, topY, botY, halfWidth, color);

		// LOW is markers only. So is MINIMAL detail, at every tier - that setting exists for
		// players who want position and nothing else.
		if (contact.drawTier == ACM_TDContactPriority.LOW)
			return labelIndex;

		if (m_Provider.GetContactDetail() == ACM_TDContactDetail.MINIMAL)
			return labelIndex;

		return DrawContactLabel(contact, workspace, centerX, topY, labelIndex, visibility);
	}

	//------------------------------------------------------------------------------------------------
	// The stale mark: a small open caret. Two lines, no text, no brackets.
	//------------------------------------------------------------------------------------------------
	protected void AddStaleMarkCommand(float centerX, float centerY, float halfWidth, int color)
	{
		float width = m_Provider.GetLineWidth();

		float arm = halfWidth * 0.45;
		if (arm < 3)
			arm = 3;

		AddLine(centerX - arm, centerY + (arm * 0.5), centerX, centerY - (arm * 0.5), color, width);
		AddLine(centerX, centerY - (arm * 0.5), centerX + arm, centerY + (arm * 0.5), color, width);
	}

	//------------------------------------------------------------------------------------------------
	// NON-COMBATANT MARKER
	//
	// Reads lastObservedPos and never the entity. There is deliberately no branch here of the kind
	// DrawContact has - no "if we can see them, use the live origin" - because the record has no
	// per-frame notion of current sight to branch on. It draws where the device last actually saw
	// them, and fades out as that observation ages.
	//
	// No outline, ever. A silhouette is earned intelligence about a threat; a civilian glowing
	// through a wall is a wallhack with good manners.
	//------------------------------------------------------------------------------------------------
	protected int DrawNonCombatant(ACM_TDNonCombatant record, WorkspaceWidget workspace, BaseWorld world, float now, int labelIndex)
	{
		if (!ACM_IffServerSettings.Allows(ACM_IffServerSettings.MASTER))
			return labelIndex;

		if (!record || !record.observed)
			return labelIndex;

		float freshness = record.Freshness(now, m_Provider.GetNonCombatantMemorySeconds());
		if (freshness <= 0)
			return labelIndex;

		vector markerPos = record.lastObservedPos + Vector(0, m_Provider.GetNonCombatantMarkerRise(), 0);

		vector screen = workspace.ProjWorldToScreen(markerPos, world, m_iProjCamera);
		if (screen[2] < NEAR_DEPTH)
			return labelIndex;

		// Same scope clip as ProjectBodyBox: while looking through a magnified optic this point was
		// projected by the scope camera, so it is only valid inside the glass.
		if (m_bScopeProj && !IsInsideScope(screen))
			return labelIndex;

		float centerX = workspace.DPIScale(screen[0]);
		float centerY = workspace.DPIScale(screen[1]);

		if (m_bProjToPanel)
			MapToPanel(workspace, centerX, centerY);

		float maskFactor = MaskFactor(workspace, centerX, centerY);
		if (maskFactor <= 0)
			return labelIndex;

		float visibility = freshness * maskFactor;

		// Unknown sits at reduced alpha on top of everything else, so it reads as the least certain
		// thing on the display rather than as a quiet civilian.
		if (record.affiliation == ACM_TDAffiliation.UNKNOWN)
			visibility = visibility * 0.6;

		int color = FadeColor(record.packedColor, visibility);
		float size = m_Provider.GetNonCombatantMarkerSize();

		// Shape carries the classification, not just colour - colour alone fails on a phosphor
		// display and on a colour-blind player.
		if (record.affiliation == ACM_TDAffiliation.NEUTRAL)
			AddSquareGlyphCommand(centerX, centerY, size, color);
		else
			AddRoleGlyphCommand(centerX, centerY, size, color);

		if (record.labelText.IsEmpty() || labelIndex >= m_aLabels.Count())
			return labelIndex;

		TextWidget label = m_aLabels[labelIndex];
		if (!label)
			return labelIndex;

		label.SetText(record.labelText);

		int packed = FadeColor(record.packedColor, visibility);
		label.SetColor(Color.FromRGBA(
			(packed >> 16) & 0xFF,
			(packed >> 8) & 0xFF,
			packed & 0xFF,
			(packed >> 24) & 0xFF));

		float textWidth, textHeight;
		label.GetTextSize(textWidth, textHeight);

		float refCenterX = workspace.DPIUnscale(centerX);
		float refCenterY = workspace.DPIUnscale(centerY);
		float refSize = workspace.DPIUnscale(size);

		float x = refCenterX + refSize + m_Provider.GetLabelOffset();
		float y = refCenterY - (textHeight * 0.5);

		float refScreenW = workspace.DPIUnscale(workspace.GetWidth());
		float refScreenH = workspace.DPIUnscale(workspace.GetHeight());

		x = Math.Clamp(x, 0, Math.Max(0, refScreenW - textWidth));
		y = Math.Clamp(y, 0, Math.Max(0, refScreenH - textHeight));

		FrameSlot.SetPos(label, x, y);
		label.SetVisible(true);

		return labelIndex + 1;
	}

	//------------------------------------------------------------------------------------------------
	// Hollow square, for a neutral third party. Deliberately not the diamond - a shape shared with
	// civilians would make the two indistinguishable in the one situation where telling them apart
	// matters, which is a busy street with a third faction on it.
	//------------------------------------------------------------------------------------------------
	protected void AddSquareGlyphCommand(float centerX, float centerY, float size, int color)
	{
		float width = m_Provider.GetLineWidth();

		float left = centerX - size;
		float right = centerX + size;
		float top = centerY - size;
		float bottom = centerY + size;

		AddLine(left, top, right, top, color, width);
		AddLine(right, top, right, bottom, color, width);
		AddLine(right, bottom, left, bottom, color, width);
		AddLine(left, bottom, left, top, color, width);
	}

	//------------------------------------------------------------------------------------------------
	// How visible a point is within the device's usable display region: 1 well inside it, ramping
	// to 0 across the feather band at its edge, and 0 outside.
	//
	// Works in the canvas' scaled pixel space, which is what WorkspaceWidget.GetWidth/GetHeight
	// return, so no conversion is needed on the way in.
	//
	// The feather exists because a hard edge reads as a glitch: a contact walking off the side of
	// the display would otherwise vanish between one frame and the next, which looks like the
	// system dropped it rather than like it left the field of view.
	//------------------------------------------------------------------------------------------------
	protected float MaskFactor(WorkspaceWidget workspace, float x, float y)
	{
		// Inside the feed the device's optic mask is meaningless - what bounds a marker is the
		// panel it is being drawn into. Hard edge, no feather: the panel has a border, so a marker
		// fading out near it would read as a rendering fault rather than as an edge.
		if (m_bProjToPanel)
		{
			if (x < 0 || x > m_fPanelW)
				return 0;

			if (y < 0 || y > m_fPanelH)
				return 0;

			return 1.0;
		}

		ACM_TDDisplayMask mask = m_Provider.GetDisplayMask();
		if (mask == ACM_TDDisplayMask.NONE)
			return 1.0;

		float screenW = workspace.GetWidth();
		float screenH = workspace.GetHeight();
		if (screenW <= 0 || screenH <= 0)
			return 1.0;

		float regionX = screenW * m_Provider.GetMaskCenterX();
		float regionY = screenH * m_Provider.GetMaskCenterY();

		// Normalised distance from the centre of the region: 0 at the centre, 1 at its edge,
		// greater than 1 outside it. Both shapes reduce to this one number so the feather maths
		// below does not care which is in use.
		float t;

		if (mask == ACM_TDDisplayMask.CIRCLE)
		{
			// Radius comes off screen HEIGHT for both axes deliberately. Scaling x by width
			// would stretch the region into an ellipse on a wide monitor, which is not what a
			// tube looks like.
			float radius = screenH * m_Provider.GetMaskRadius();
			if (radius <= 0)
				return 0;

			float dx = x - regionX;
			float dy = y - regionY;

			t = Math.Sqrt((dx * dx) + (dy * dy)) / radius;
		}
		else
		{
			float halfW = screenW * m_Provider.GetMaskHalfWidth();
			float halfH = screenH * m_Provider.GetMaskHalfHeight();
			if (halfW <= 0 || halfH <= 0)
				return 0;

			// Whichever axis runs out first defines the edge.
			float tx = Math.AbsFloat(x - regionX) / halfW;
			float ty = Math.AbsFloat(y - regionY) / halfH;

			t = Math.Max(tx, ty);
		}

		if (t >= 1.0)
			return 0;

		float feather = Math.Clamp(m_Provider.GetMaskFeather(), 0, 0.9);
		if (feather <= 0)
			return 1.0;

		float fadeStart = 1.0 - feather;
		if (t <= fadeStart)
			return 1.0;

		return (1.0 - t) / feather;
	}

	//------------------------------------------------------------------------------------------------
	// Project a ground position into the screen-space box the renderer draws.
	//
	// Same construction the Shadow Admin ESP uses: project the feet and a fixed body height
	// above them, take the vertical span as the box height, and derive the width from that span
	// rather than from anything about the target itself. A box built this way holds its
	// proportions at every range and needs nothing from the character's pose or rig.
	//
	// Returns false when the contact is behind the camera or too small on screen to be worth
	// drawing, in which case the out parameters are meaningless.
	//------------------------------------------------------------------------------------------------
	// A projection through another camera comes back as if that camera filled the screen, so it
	// has to be squeezed into the panel the render target actually occupies.
	//
	// The horizontal correction matters and is easy to miss: the camera's field of view is
	// VERTICAL, so the horizontal extent depends on the aspect of whatever it is rendered into.
	// Projecting against the screen's aspect and drawing into a panel of a different one puts every
	// marker sideways of its target by an amount that grows toward the edges.
	//------------------------------------------------------------------------------------------------
	protected void MapToPanel(WorkspaceWidget workspace, inout float x, inout float y)
	{
		float screenW = workspace.GetWidth();
		float screenH = workspace.GetHeight();
		if (screenW <= 0 || screenH <= 0 || m_fPanelH <= 0)
			return;

		float screenAspect = screenW / screenH;
		float panelAspect = m_fPanelW / m_fPanelH;

		// Normalised, centre-relative, so the aspect correction is a straight scale about the
		// middle of the view rather than about its corner.
		float nx = (x / screenW) - 0.5;
		float ny = (y / screenH) - 0.5;

		if (panelAspect > 0)
			nx = nx * (screenAspect / panelAspect);

		// Relative to the canvas, which is what a draw command is measured against.
		x = (nx + 0.5) * m_fPanelW;
		y = (ny + 0.5) * m_fPanelH;
	}

	//------------------------------------------------------------------------------------------------
	//------------------------------------------------------------------------------------------------
	// Decide which camera this frame's markers project through.
	//
	// Called once per draw pass, before any marker is projected, so the whole pass is consistent -
	// half the marks through one camera and half through another would be worse than either.
	//
	// The feed pass overrides m_iProjCamera for itself afterwards and restores it to -1 when it is
	// done, so this and the feed do not fight: this runs first, the feed runs last.
	//------------------------------------------------------------------------------------------------
	protected void UpdateScopeProjection()
	{
		m_bScopeProj = false;
		m_iProjCamera = -1;
		m_ScopeSights = null;

		if (!SCR_2DPIPSightsComponent.IsPIPActive())
			return;

		SCR_2DPIPSightsComponent sights = ArmaReforgerScripted.GetCurrentPIPSights();
		if (!sights)
			return;

		int cam = sights.GetPIPCameraIndex();
		if (cam < 0)
			return;

		m_iProjCamera = cam;
		m_bScopeProj = true;
		m_ScopeSights = sights;
	}

	//------------------------------------------------------------------------------------------------
	// True when this screen point is inside the scope glass. Only meaningful while m_bScopeProj.
	//
	// The point passed in must be RAW ProjWorldToScreen output - reference resolution, before
	// DPIScale - because that is the space the sight component reports its own geometry in. Feeding
	// it scaled pixels would shrink or grow the circle by the DPI factor and clip the wrong marks.
	protected bool IsInsideScope(vector rawScreenPos)
	{
		if (!m_ScopeSights)
			return true;

		return m_ScopeSights.IsScreenPositionInSights(rawScreenPos);
	}

	protected bool ProjectBodyBox(vector groundPos, WorkspaceWidget workspace, BaseWorld world, out float centerX, out float topY, out float botY, out float halfWidth)
	{
		vector head = groundPos + Vector(0, BOX_BODY_HEIGHT_M, 0);

		vector screenFeet = workspace.ProjWorldToScreen(groundPos, world, m_iProjCamera);
		vector screenHead = workspace.ProjWorldToScreen(head, world, m_iProjCamera);

		if (screenFeet[2] < NEAR_DEPTH || screenHead[2] < NEAR_DEPTH)
			return false;

		// Canvas commands are in scaled pixels, while ProjWorldToScreen hands back
		// reference-resolution coordinates, so both ends go through DPIScale before any
		// arithmetic happens on them.
		float feetX = workspace.DPIScale(screenFeet[0]);
		float feetY = workspace.DPIScale(screenFeet[1]);
		float headX = workspace.DPIScale(screenHead[0]);
		float headY = workspace.DPIScale(screenHead[1]);

		if (m_bProjToPanel)
		{
			MapToPanel(workspace, feetX, feetY);
			MapToPanel(workspace, headX, headY);
		}

		float height = feetY - headY;
		if (height < m_Provider.GetMinMarkerHeight())
			return false;

		// Looking through a magnified optic: the surrounding image is the MAIN camera's, so a mark
		// projected by the scope camera is only valid inside the scope glass. Tested at the body's
		// mid-point, in raw projection space, so a target half in the circle still marks rather
		// than flickering on its edge.
		if (m_bScopeProj)
		{
			vector rawMid = (screenFeet + screenHead) * 0.5;
			if (!IsInsideScope(rawMid))
				return false;
		}

		// Scaled about the marker's own centre rather than about its feet, so turning the scale
		// down produces a smaller mark still centred on the body instead of one that slides down
		// the target's legs.
		float scale = m_Provider.GetMarkerScale();
		float midY = (headY + feetY) * 0.5;
		float halfHeight = height * 0.5 * scale;

		centerX = (feetX + headX) * 0.5;
		topY = midY - halfHeight;
		botY = midY + halfHeight;
		halfWidth = (height * scale) * m_Provider.GetBoxAspect();

		return true;
	}

	//------------------------------------------------------------------------------------------------
	// The contact box: the four corners of a rectangle, with the middle of every side left
	// open. Corners rather than a closed rectangle because this marks a POSITION THE TARGET WAS
	// LAST SEEN AT, not a target - brackets read as an estimate in a way a solid box does not.
	//------------------------------------------------------------------------------------------------
	protected void AddCornerBoxCommand(float centerX, float topY, float botY, float halfWidth, int color)
	{
		float left = centerX - halfWidth;
		float right = centerX + halfWidth;
		float width = m_Provider.GetLineWidth();

		float fraction = m_Provider.GetCornerFraction();
		float armX = halfWidth * 2 * fraction;
		float armY = (botY - topY) * fraction;

		AddLine(left, topY, left + armX, topY, color, width);
		AddLine(left, topY, left, topY + armY, color, width);

		AddLine(right, topY, right - armX, topY, color, width);
		AddLine(right, topY, right, topY + armY, color, width);

		AddLine(left, botY, left + armX, botY, color, width);
		AddLine(left, botY, left, botY - armY, color, width);

		AddLine(right, botY, right - armX, botY, color, width);
		AddLine(right, botY, right, botY - armY, color, width);
	}

	//------------------------------------------------------------------------------------------------
	// The engine outliner, and the only place in this file that touches it.
	//
	// Three gates, and all three have to hold: the device setting is on, the wearer has sight of
	// this contact RIGHT NOW, and there is still an entity to outline. OutlineEntity renders
	// through geometry, so pointing it at a remembered or a shared contact would hand every
	// wearer a wallhack - which is precisely the thing this system exists not to be.
	//------------------------------------------------------------------------------------------------
	// Returns TRUE when the body has actually been marked, so the caller knows whether to fall
	// through to the corner box. It used to return nothing and the caller assumed success, which
	// meant a contact whose outline was refused silently got no body mark at all.
	protected bool DrawEntityOutline(ACM_TDContact contact, float maskFactor)
	{
		if (!m_Provider.OutlinesConfirmedContacts() || !contact.HasLiveSight())
			return false;

		if (maskFactor <= 0)
			return false;

		// THE ENGINE OUTLINER CANNOT REACH A SCOPE.
		//
		// OutlineEntity is a main-view pass. Looking through a 2D PIP optic, the glass shows a
		// DIFFERENT camera's render, so the silhouette is drawn where the body sits in the
		// unmagnified main view while the body you can see sits somewhere else entirely inside the
		// circle. That is the offset in the screenshots, and no amount of coordinate mapping fixes
		// it - the outline is not in the same image as the target.
		//
		// So while a scope is up the outline is not drawn at all, and returning false hands the
		// contact to the corner box, which IS projected through the scope camera and does land on
		// the magnified body.
		//
		// TEMPORARY probe: report what material components the contact carries, once a second. A
		// mesh-level highlight is a material change rather than a screen pass, so it would render
		// in the scope render target - but only if the character actually has the component.
		if (m_bScopeProj)
			return LightScopedHighlight(contact.entity, contact.labelColor, maskFactor);

		ChimeraWorld world = ChimeraWorld.CastFrom(GetGame().GetWorld());
		if (!world || !contact.labelColor)
			return false;

		// Masked by opacity rather than by culling. The outliner works on a world entity, so it
		// cannot be clipped to a screen region - but fading it out across the same feather band
		// the markers use keeps a silhouette from glowing in the black beyond the display edge.
		// Approximate at the boundary, since a target straddling the edge is judged on one point.
		world.OutlineEntity(contact.entity, contact.labelColor, maskFactor, 0.15 * maskFactor, true);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	// Label, centred above the box. The string itself was built on the scan tick - range for a
	// contact in sight, age for a remembered one - so nothing here allocates.
	//------------------------------------------------------------------------------------------------
	protected int DrawContactLabel(ACM_TDContact contact, WorkspaceWidget workspace, float centerX, float topY, int labelIndex, float visibility)
	{
		if (!m_Provider.DrawsLabel() || contact.labelText.IsEmpty() || labelIndex >= m_aLabels.Count())
			return labelIndex;

		TextWidget label = m_aLabels[labelIndex];
		if (!label)
			return labelIndex;

		label.SetText(contact.labelText);

		if (contact.labelColor)
		{
			int packed = FadeColor(contact.labelColor.PackToInt(), visibility);
			label.SetColor(Color.FromRGBA(
				(packed >> 16) & 0xFF,
				(packed >> 8) & 0xFF,
				packed & 0xFF,
				(packed >> 24) & 0xFF));
		}

		float textWidth, textHeight;
		label.GetTextSize(textWidth, textHeight);

		// FrameSlot positions in reference units rather than the scaled pixels the canvas
		// works in, so the box coordinates come back down through DPIUnscale before use.
		float refCenterX = workspace.DPIUnscale(centerX);
		float refTopY = workspace.DPIUnscale(topY);

		float x = refCenterX - (textWidth * 0.5);
		float y = refTopY - textHeight - m_Provider.GetLabelOffset();

		// Kept on screen. Without this a contact near the top edge pushes its label off the top -
		// the marker is inside the display region but the text sits above it, so the range readout
		// silently disappears exactly when a target is closest to the edge of view.
		float refScreenW = workspace.DPIUnscale(workspace.GetWidth());
		float refScreenH = workspace.DPIUnscale(workspace.GetHeight());

		x = Math.Clamp(x, 0, Math.Max(0, refScreenW - textWidth));
		y = Math.Clamp(y, 0, Math.Max(0, refScreenH - textHeight));

		FrameSlot.SetPos(label, x, y);
		label.SetVisible(true);

		return labelIndex + 1;
	}

	//------------------------------------------------------------------------------------------------
	// Scale an ARGB colour's alpha by a 0..1 factor, leaving the RGB alone.
	//------------------------------------------------------------------------------------------------
	protected int FadeColor(int packed, float factor)
	{
		int alpha = (packed >> 24) & 0xFF;
		int faded = Math.Round(alpha * Math.Clamp(factor, 0, 1));

		return (packed & 0x00FFFFFF) | (faded << 24);
	}

	//------------------------------------------------------------------------------------------------
	protected void AddLine(float x0, float y0, float x1, float y1, int color, float width)
	{
		LineDrawCommand line = new LineDrawCommand();
		line.m_Vertices = { x0, y0, x1, y1 };
		line.m_iColor = color;
		line.m_fWidth = width;
		if (!m_aActiveCommands)
			m_aActiveCommands = CurrentDrawBuffer();

		m_aActiveCommands.Insert(line);
	}


	//------------------------------------------------------------------------------------------------
	// NON-COMBATANTS
	//
	// Civilians, characters with no resolvable faction, and neutral third parties. Observed, never
	// tracked.
	//
	// The one rule that governs this whole path: NOTHING here reads entity.GetOrigin() except the
	// trace, and the trace writes what it found into lastObservedPos. The draw pass reads
	// lastObservedPos and has no route back to the live entity at all. That is what makes the memory
	// window a memory of a position rather than a short wall-tracker - a one-second wall-tracker is
	// still a wall-tracker.
	//
	// What this path deliberately does not have: dwell, confidence, a contact id, a network
	// identity, any route to the data link, and any outline. Those are not switched off here; the
	// record type has no fields for them.
	//------------------------------------------------------------------------------------------------
	protected void ConsiderNonCombatant(IEntity candidate, ACM_TDAffiliation affiliation, float distance)
	{
		if (!ACM_IffServerSettings.Allows(ACM_IffServerSettings.MASTER))
			return;

		if (distance > m_Provider.GetCivilianMaxRange())
			return;

		// Presentation gates. Classification has already happened and is not revisited - a civilian
		// stays a civilian whatever any of these say, and cannot be reached by the hostile pipeline
		// from here whatever the wearer has switched on.
		ACM_TDContactFilter filter = m_Provider.GetContactFilter();

		if (affiliation == ACM_TDAffiliation.CIVILIAN)
		{
			if (!m_Provider.IsCivilianIffEnabled())
				return;

			if (filter == ACM_TDContactFilter.THREATS)
				return;
		}
		else if (affiliation == ACM_TDAffiliation.UNKNOWN)
		{
			if (!m_Provider.ShowsUnknownContacts())
				return;

			if (filter == ACM_TDContactFilter.THREATS)
				return;

			// An unresolved character at range is almost always one that has not finished streaming
			// in, and a display full of question marks at 140m is noise rather than information. At
			// RELEVANT they are only worth drawing close enough to matter.
			if (filter == ACM_TDContactFilter.RELEVANT && distance > m_Provider.GetCivilianMaxRange() * 0.5)
				return;
		}
		else
		{
			// Neutral third parties. Never at RELEVANT - a declared non-belligerent side is exactly
			// the thing a busy display can do without.
			if (filter != ACM_TDContactFilter.ALL)
				return;
		}

		ACM_TDNonCombatant record = FindExistingNonCombatant(candidate);
		if (!record)
			record = new ACM_TDNonCombatant(candidate, affiliation);

		record.entity = candidate;
		record.scanDistance = distance;

		// Re-classified every scan, like a contact is. A character can change perceived faction by
		// changing clothes, and a record that remembered its first answer would keep the old glyph.
		record.affiliation = affiliation;

		m_aNonCombatantScan.Insert(record);
	}

	//------------------------------------------------------------------------------------------------
	protected ACM_TDNonCombatant FindExistingNonCombatant(IEntity entity)
	{
		foreach (ACM_TDNonCombatant record : m_aNonCombatants)
		{
			if (record && record.entity == entity)
				return record;
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	// Cap first, then trace, then cache the presentation. Capping before tracing is the point of
	// the ordering: raycasts are the expensive part and there is no reason to spend one on a record
	// that was never going to be drawn.
	//------------------------------------------------------------------------------------------------
	protected void FinaliseNonCombatants(BaseWorld world, vector camPos, float now)
	{
		SortNonCombatantsByDistance(m_aNonCombatantScan);

		while (m_aNonCombatantScan.Count() > m_Provider.GetMaxNonCombatants())
			m_aNonCombatantScan.Remove(m_aNonCombatantScan.Count() - 1);

		ResolveNonCombatantSight(world, camPos, now);

		foreach (ACM_TDNonCombatant record : m_aNonCombatantScan)
			RefreshNonCombatant(record, now);

		array<ref ACM_TDNonCombatant> kept = {};
		foreach (ACM_TDNonCombatant survivor : m_aNonCombatantScan)
			kept.Insert(survivor);

		m_aNonCombatants = kept;
	}

	//------------------------------------------------------------------------------------------------
	// The non-combatant trace budget.
	//
	// Same two-phase shape as the hostile pass - never-traced records first, then round robin over
	// the rest - but on its own cursor and its own budget. A street full of civilians must not be
	// able to delay a hostile line-of-sight check by a single scan.
	//
	// A clear trace is the ONLY thing that writes a position here.
	//------------------------------------------------------------------------------------------------
	protected void ResolveNonCombatantSight(BaseWorld world, vector camPos, float now)
	{
		int count = m_aNonCombatantScan.Count();
		if (count == 0)
			return;

		IEntity localEntity = SCR_PlayerController.GetLocalControlledEntity();

		// A device that does not require line of sight still gets one, because these markers have
		// no other gate. The hostile path can afford an open setting - it has dwell, confidence and
		// an expiry behind it. A non-combatant marker is nothing but "the device can see them", so
		// removing the trace would leave it as nothing but "somebody is over there, through that
		// wall".
		int budget = m_Provider.GetNonCombatantTracesPerScan();
		if (budget > count)
			budget = count;

		int spent = 0;

		for (int i = 0; i < count && spent < budget; i++)
		{
			ACM_TDNonCombatant fresh = m_aNonCombatantScan[i];
			if (!fresh || fresh.traced)
				continue;

			TraceNonCombatant(world, camPos, fresh, localEntity, now);
			spent++;
		}

		for (int i = 0; i < count && spent < budget; i++)
		{
			int index = (m_iNonCombatantTraceCursor + i) % count;
			ACM_TDNonCombatant record = m_aNonCombatantScan[index];
			if (!record || !record.entity)
				continue;

			TraceNonCombatant(world, camPos, record, localEntity, now);
			spent++;
		}

		m_iStatNonCombatantTraces = spent;

		int advance = spent;
		if (advance < 1)
			advance = 1;

		m_iNonCombatantTraceCursor = (m_iNonCombatantTraceCursor + advance) % count;
	}

	//------------------------------------------------------------------------------------------------
	// One raycast, and the single writer of lastObservedPos.
	//
	// An occluded record is left completely untouched. Not marked, not moved, not aged by hand -
	// the stored position simply stands and the memory window runs out from the last time this
	// method succeeded.
	//------------------------------------------------------------------------------------------------
	protected void TraceNonCombatant(BaseWorld world, vector camPos, ACM_TDNonCombatant record, IEntity localEntity, float now)
	{
		if (!record || !record.entity)
			return;

		record.traced = true;

		vector livePos = record.entity.GetOrigin();
		vector aimPoint = livePos + Vector(0, TRACE_AIM_HEIGHT_M, 0);

		if (!TraceVisibility(world, camPos, aimPoint, localEntity, record.entity))
			return;

		record.RecordObservation(livePos, now, vector.Distance(camPos, livePos));

		// First confirmed look only. A civilian walking in and out of view all afternoon is one
		// line in the log, not forty. Announced is never cleared, so re-observation stays silent
		// for the life of the record.
		if (!record.announced)
		{
			record.announced = true;

			string what = "NON-COMBATANT";
			if (record.affiliation == ACM_TDAffiliation.CIVILIAN)
				what = "CIVILIAN";
			else if (record.affiliation == ACM_TDAffiliation.UNKNOWN)
				what = "UNIDENTIFIED";

			ACM_IvasEventLog.Push(ACM_EIvasEventKind.CIVILIAN, string.Format("%1  %2m", what, Math.Round(record.distance)));
		}
	}

	//------------------------------------------------------------------------------------------------
	// Cached colour and label, rebuilt on the same interval friendlies use. The glyph shape carries
	// the classification on its own, so the text is a courtesy rather than the identification.
	//------------------------------------------------------------------------------------------------
	protected void RefreshNonCombatant(ACM_TDNonCombatant record, float now)
	{
		if (!record)
			return;

		if (record.nextRefreshTime > 0 && now < record.nextRefreshTime)
			return;

		record.nextRefreshTime = now + m_Provider.GetFriendlyRefreshSeconds();

		Color color = m_Provider.GetAffiliationColor(record.affiliation);
		if (!color)
			color = Color.FromRGBA(220, 210, 180, 200);

		record.color = color;
		record.packedColor = color.PackToInt();
		record.labelText = BuildNonCombatantLabel(record);
	}

	//------------------------------------------------------------------------------------------------
	protected string BuildNonCombatantLabel(ACM_TDNonCombatant record)
	{
		if (!m_Provider.DrawsLabel())
			return string.Empty;

		if (record.distance > m_Provider.GetLabelMaxRange())
			return string.Empty;

		string tag = string.Empty;
		if (record.affiliation == ACM_TDAffiliation.CIVILIAN)
			tag = "CIV";
		else if (record.affiliation == ACM_TDAffiliation.UNKNOWN)
			tag = "UNK";

		// Neutrals get no text at all. A third military side is identified by being marked at all;
		// spelling it out adds a word to a display that is already the thing being decluttered.
		if (tag.IsEmpty())
			return string.Empty;

		if (!m_Provider.LabelShowsDistance())
			return tag;

		return string.Format("%1  %2m", tag, Math.Round(record.distance));
	}

	//------------------------------------------------------------------------------------------------
	protected void SortNonCombatantsByDistance(array<ref ACM_TDNonCombatant> records)
	{
		for (int i = 1; i < records.Count(); i++)
		{
			ref ACM_TDNonCombatant key = records[i];
			int j = i - 1;

			while (j >= 0 && records[j].scanDistance > key.scanDistance)
			{
				records[j + 1] = records[j];
				j--;
			}

			records[j + 1] = key;
		}
	}

	//------------------------------------------------------------------------------------------------
	// HOSTILE PRESENTATION PRIORITY
	//
	// Written once per scan onto the contact and read by the draw pass. Ranking two dozen contacts
	// sixty times a second to answer a question that changes ten times a second is work for nothing,
	// and it is also how a marker ends up flickering between two tiers.
	//
	// Every input is state the contact already carries. Nothing new is measured here.
	//------------------------------------------------------------------------------------------------
	protected ACM_TDContactPriority ComputeContactPriority(ACM_TDContact contact, float now)
	{
		if (!contact)
			return ACM_TDContactPriority.STALE;

		float age = contact.AgeSeconds(now);
		if (age < 0)
			age = 0;

		float stale = m_Provider.GetStaleAgeSeconds();

		// Live sight, close. The thing about to shoot you, and the reason this whole mechanism
		// exists - a nearby confirmed threat must not lose its marker because a drone just filed
		// twenty contacts on the other side of the valley.
		if (contact.HasLiveSight())
		{
			if (contact.distance <= m_Provider.GetCriticalRangeMetres())
				return ACM_TDContactPriority.CRITICAL;

			return ACM_TDContactPriority.HIGH;
		}

		// Shared intelligence. It has no local confidence curve to read, so it is ranked on age
		// alone, and it is capped below HIGH: somebody else's report is worth knowing and is not
		// worth as much as the wearer's own eyes.
		if (contact.delivery != ACM_TDDelivery.LOCAL)
		{
			if (age >= stale)
				return ACM_TDContactPriority.STALE;

			if (age >= stale * 0.5)
				return ACM_TDContactPriority.LOW;

			return ACM_TDContactPriority.NORMAL;
		}

		if (age >= stale)
			return ACM_TDContactPriority.STALE;

		// Legacy contacts carry no confidence model, so they read as fresh local intelligence until
		// the age test above catches them.
		float confidence = 1.0;
		if (contact.ivas)
			confidence = ConfidenceFor(contact, now);

		if (confidence >= 0.80)
			return ACM_TDContactPriority.HIGH;

		if (confidence >= m_Provider.GetNormalConfidenceThreshold())
			return ACM_TDContactPriority.NORMAL;

		if (confidence >= m_Provider.GetLowConfidenceThreshold())
			return ACM_TDContactPriority.LOW;

		return ACM_TDContactPriority.STALE;
	}

	//------------------------------------------------------------------------------------------------
	// CLUTTER BUDGETS
	//
	// THE store is not touched here, and that is the whole contract. A contact squeezed off a busy
	// display keeps its network id, keeps ageing, keeps deduping against incoming reports and keeps
	// its confidence curve. Every decision below writes drawTier and drawVisible and nothing else.
	//
	// Deleting a contact to tidy the screen would turn "my display is busy" into a claim about what
	// the squad knows, and a receiver that dropped shared contacts would make the link lie.
	//------------------------------------------------------------------------------------------------
	protected void RankContactsForPresentation(float now)
	{
		foreach (ACM_TDContact contact : m_aContacts)
		{
			if (!contact)
				continue;

			contact.priority = ComputeContactPriority(contact, now);
			contact.drawTier = contact.priority;
			contact.drawVisible = false;
		}

		m_aRankedContacts.Clear();
		foreach (ACM_TDContact ranked : m_aContacts)
		{
			if (ranked)
				m_aRankedContacts.Insert(ranked);
		}

		SortContactsByPriority(m_aRankedContacts);

		// Hostile tracking off. Every contact stays exactly where it is - still ageing, still
		// deduping, still receiving shared reports - and none of them draw. Leaving them visible
		// while the local sensor stopped acquiring would have made the switch look half-broken:
		// nothing new appears, but everything already found stays on the display forever.
		if (!m_Provider.IsHostileTrackingEnabled())
			return;

		bool showStale = m_Provider.ShowsStaleContacts();
		int fullBudget = m_Provider.GetFullDetailBudget();
		int visibleBudget = m_Provider.GetVisibleContactBudget();

		int fullUsed = 0;
		int visibleUsed = 0;

		foreach (ACM_TDContact entry : m_aRankedContacts)
		{
			if (!entry)
				continue;

			// A candidate part-way through dwell is in the store so its timer survives. It is not a
			// contact yet and must not consume a budget slot from one that is.
			if (entry.ivas && !entry.acquired)
				continue;

			if (entry.priority == ACM_TDContactPriority.STALE && !showStale)
				continue;

			// CRITICAL is exempt, in both directions. It always draws, always at full detail, and it
			// still consumes from the counters - so pressure falls on the tiers below it rather than
			// on the contact that most needs to be seen.
			if (entry.priority == ACM_TDContactPriority.CRITICAL)
			{
				entry.drawVisible = true;
				entry.drawTier = ACM_TDContactPriority.CRITICAL;
				fullUsed++;
				visibleUsed++;
				m_iStatFullDetail++;
				continue;
			}

			if (visibleUsed >= visibleBudget)
			{
				m_iStatSuppressed++;
				continue;
			}

			visibleUsed++;
			entry.drawVisible = true;

			if (fullUsed < fullBudget)
			{
				fullUsed++;
				m_iStatFullDetail++;
				continue;
			}

			// Out of label budget. One tier down rather than gone: the marker still says where, it
			// just stops saying how far and how old.
			entry.drawTier = DowngradeTier(entry.priority);
			m_iStatDowngraded++;
		}
	}

	//------------------------------------------------------------------------------------------------
	// One step quieter, floored at STALE. Never applied to CRITICAL.
	//------------------------------------------------------------------------------------------------
	protected ACM_TDContactPriority DowngradeTier(ACM_TDContactPriority tier)
	{
		if (tier == ACM_TDContactPriority.HIGH)
			return ACM_TDContactPriority.NORMAL;

		if (tier == ACM_TDContactPriority.NORMAL)
			return ACM_TDContactPriority.LOW;

		return ACM_TDContactPriority.STALE;
	}

	//------------------------------------------------------------------------------------------------
	// Strongest first, nearest first inside a tier. The enum is declared in presentation order, so
	// this is a plain integer comparison rather than a table of special cases.
	//------------------------------------------------------------------------------------------------
	protected void SortContactsByPriority(array<ACM_TDContact> contacts)
	{
		for (int i = 1; i < contacts.Count(); i++)
		{
			ACM_TDContact key = contacts[i];
			int j = i - 1;

			while (j >= 0 && ComesAfter(contacts[j], key))
			{
				contacts[j + 1] = contacts[j];
				j--;
			}

			contacts[j + 1] = key;
		}
	}

	//------------------------------------------------------------------------------------------------
	protected bool ComesAfter(ACM_TDContact a, ACM_TDContact b)
	{
		int left = a.priority;
		int right = b.priority;

		if (left != right)
			return left > right;

		return a.distance > b.distance;
	}

	//------------------------------------------------------------------------------------------------
	// FRIENDLY IFF
	//
	// A separate, deliberately cheap path. Faction decides who is eligible; the IFF mode decides
	// who is shown; the player's declared tactical role decides how they look. Those three run in
	// that order and never fold into each other.
	//
	// What this path does NOT do, by design: no line-of-sight raycasts, no acquisition dwell, no
	// confidence decay, no frozen positions, no bone reads, no hull building. A friendly is not
	// earned intelligence - they are on your side and their position is simply known. The whole
	// cost is one projection per friendly per frame plus a cached string rebuilt about once a
	// second.
	//------------------------------------------------------------------------------------------------
	protected void ConsiderFriendly(IEntity candidate, float distance, float now, bool trace = false)
	{
		if (m_Provider.GetFriendlyIffMode() == ACM_TDFriendlyIffMode.OFF)
		{
			if (trace)
				Print("[ACM IFF]   rejected: mode is OFF", LogLevel.DEBUG);

			return;
		}

		// Friendly range is its own setting. Knowing where your squad is stays useful far past the
		// range at which spotting an enemy does.
		if (distance > m_Provider.GetFriendlyMaxRange())
		{
			if (trace)
				Print(string.Format("[ACM IFF]   rejected: %1m beyond friendly range %2m", distance, m_Provider.GetFriendlyMaxRange()), LogLevel.DEBUG);

			return;
		}

		PlayerManager playerManager = GetGame().GetPlayerManager();

		// -1 for friendly AI, which is not an error: AI have no player and therefore no declared
		// role, so they fall through to UNASSIGNED and draw in the plain friendly colour. They are
		// still worth marking - a squad's AI are exactly who you do not want to shoot.
		int playerId = -1;
		if (playerManager)
			playerId = playerManager.GetPlayerIdFromControlledEntity(candidate);

		if (!PassesIffMode(playerId, candidate))
		{
			if (trace)
				Print(string.Format("[ACM IFF]   rejected: failed IFF mode %1 (playerId=%2)", SCR_Enum.GetEnumName(ACM_TDFriendlyIffMode, m_Provider.GetFriendlyIffMode()), playerId), LogLevel.DEBUG);

			return;
		}

		ACM_TDFriendly friendly = FindExistingFriendly(candidate);
		if (!friendly)
			friendly = new ACM_TDFriendly(candidate, playerId);

		friendly.entity = candidate;
		friendly.playerId = playerId;
		friendly.distance = distance;

		RefreshFriendly(friendly, now);

		m_aFriendlyScan.Insert(friendly);

		if (trace)
			Print(string.Format("[ACM IFF]   ACCEPTED playerId=%1 role=%2 label='%3'", playerId, SCR_Enum.GetEnumName(ACM_ETacticalRole, friendly.role), friendly.labelText), LogLevel.DEBUG);
	}

	//------------------------------------------------------------------------------------------------
	// Visibility, decided before any presentation work happens.
	//------------------------------------------------------------------------------------------------
	protected bool PassesIffMode(int playerId, IEntity candidate)
	{
		ACM_TDFriendlyIffMode mode = m_Provider.GetFriendlyIffMode();

		if (mode == ACM_TDFriendlyIffMode.ALL_FRIENDLIES)
			return true;

		PlayerController controller = GetGame().GetPlayerController();
		if (!controller)
			return false;

		SCR_GroupsManagerComponent groups = SCR_GroupsManagerComponent.GetInstance();
		if (!groups)
			return false;

		SCR_AIGroup myGroup = groups.GetPlayerGroup(controller.GetPlayerId());
		if (!myGroup)
			return false;

		if (mode == ACM_TDFriendlyIffMode.GROUP_LEADER)
			return playerId > 0 && playerId == myGroup.GetLeaderID();

		// GROUP.
		//
		// Players resolve by id. AI resolve by walking the group's own agent list, which is the
		// part that was missing: GetPlayerGroup only answers for players, so every AI squadmate
		// failed the test and the entire mode drew nothing in any scenario whose friendlies are
		// AI - which is most of them. The agent walk is bounded by squad size and only runs on the
		// IFF refresh tick, so it is not worth caching.
		if (playerId > 0)
		{
			SCR_AIGroup theirGroup = groups.GetPlayerGroup(playerId);

			return theirGroup && theirGroup.GetGroupID() == myGroup.GetGroupID();
		}

		array<AIAgent> agents = {};
		myGroup.GetAgents(agents);

		foreach (AIAgent agent : agents)
		{
			if (agent && agent.GetControlledEntity() == candidate)
				return true;
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected ACM_TDFriendly FindExistingFriendly(IEntity entity)
	{
		foreach (ACM_TDFriendly friendly : m_aFriendlies)
		{
			if (friendly && friendly.entity == entity)
				return friendly;
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	// Rebuild the cached role, colour and label text - but only when the refresh interval says so.
	//
	// This is the reason friendly IFF is cheap. A name, a role and a group change on the order of
	// never, while the draw pass runs sixty times a second; rebuilding those strings per frame per
	// friendly would dwarf every other cost in this file. Position still projects every frame, so
	// the markers stay glued to moving players regardless.
	//------------------------------------------------------------------------------------------------
	protected void RefreshFriendly(ACM_TDFriendly friendly, float now)
	{
		if (friendly.nextRefreshTime > 0 && now < friendly.nextRefreshTime)
			return;

		friendly.nextRefreshTime = now + m_Provider.GetFriendlyRefreshSeconds();

		// The EFFECTIVE role, not the declared one. A player who has chosen a role gets exactly
		// what they chose; one who has not gets a conservative guess derived locally from what
		// this client can see of them, and that guess is never written back or replicated.
		//
		// Resolved here, on the refresh tick, rather than in the draw pass - inference walks an
		// inventory and inspects a vehicle seat, which is fine once a second per friendly and
		// absurd sixty times a second per friendly.
		// Static resolver, NOT the instance one. A world whose active game mode does not carry the
		// registry component still gets role-coloured friendlies from local inference; it just
		// cannot store or replicate a role anyone has declared.
		friendly.role = ACM_TacticalRoleComponent.ResolveEffectiveRole(friendly.playerId, friendly.entity);

		WarnOnceIfNoRoleRegistry();

		ACM_TacticalRolePreset preset = ACM_TacticalRoleCatalog.GetPreset(friendly.role);

		// Defensive only - neither the resolver nor inference returns UNASSIGNED any more. If it
		// somehow does, fall back to the device's plain friendly colour rather than to a role
		// preset that would imply a role nobody holds.
		if (friendly.role == ACM_ETacticalRole.UNASSIGNED)
			friendly.color = m_Provider.GetAffiliationColor(ACM_TDAffiliation.FRIENDLY);
		else
			friendly.color = preset.m_Color;

		if (!friendly.color)
			friendly.color = Color.FromRGBA(150, 220, 150, 255);

		friendly.packedColor = friendly.color.PackToInt();
		friendly.priority = preset.m_iPriority;

		// Before the label, because DETAILED labels may want to carry it later.
		ResolveFriendlyStatus(friendly, now);

		friendly.labelText = BuildFriendlyLabel(friendly, preset);
	}

	//------------------------------------------------------------------------------------------------
	// What a squadmate is doing, for the visor's status strip.
	//
	// Read straight off their own character controller - the same component the local player's own
	// HUD reads, and state the engine already replicates so remote characters animate correctly.
	// Nothing here asks the network for anything or infers from AI internals.
	//
	// Ordered worst-first and FIRST MATCH WINS. A downed squadmate who happens to be in a vehicle is
	// DOWN, not MOUNTED - the ordering is the whole judgement, and it is deliberately about what the
	// wearer most needs to know rather than about what is most specific.
	//------------------------------------------------------------------------------------------------
	protected void ResolveFriendlyStatus(ACM_TDFriendly friendly, float now)
	{
		if (!friendly || !friendly.entity)
			return;

		SampleFriendlySpeed(friendly, now);

		SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(friendly.entity.FindComponent(SCR_CharacterControllerComponent));
		if (!controller)
		{
			friendly.status = ACM_ETacticalStatus.UNKNOWN;
			return;
		}

		if (controller.IsDead())
		{
			friendly.status = ACM_ETacticalStatus.KIA;
			return;
		}

		if (controller.IsUnconscious() || controller.GetLifeState() == ECharacterLifeState.INCAPACITATED)
		{
			friendly.status = ACM_ETacticalStatus.DOWN;
			return;
		}

		// Weapon up outranks being in a seat, because a gunner engaging is the more urgent fact.
		if (controller.IsWeaponRaised())
		{
			friendly.status = ACM_ETacticalStatus.ENGAGING;
			return;
		}

		CompartmentAccessComponent access = CompartmentAccessComponent.Cast(friendly.entity.FindComponent(CompartmentAccessComponent));
		if (access && access.IsInCompartment())
		{
			friendly.status = ACM_ETacticalStatus.MOUNTED;
			return;
		}

		if (friendly.speed >= SPRINT_SPEED_MS)
		{
			friendly.status = ACM_ETacticalStatus.SPRINTING;
			return;
		}

		if (friendly.speed >= MOVING_SPEED_MS)
		{
			friendly.status = ACM_ETacticalStatus.MOVING;
			return;
		}

		friendly.status = ACM_ETacticalStatus.HOLDING;
	}

	//------------------------------------------------------------------------------------------------
	// Metres per second, from the distance covered since the last sample.
	//
	// A position delta rather than a physics query, because the sample interval already exists and
	// this works on any character whether or not it currently has an active physics body.
	//
	// The teleport guard is not hypothetical in this project: a Game Master move puts a squadmate
	// two kilometres away between one sample and the next, and without this every teleport would
	// read as SPRINTING for one refresh. An impossible speed is treated as a discontinuity - the
	// baseline is re-established and no speed is reported for that sample.
	//------------------------------------------------------------------------------------------------
	protected void SampleFriendlySpeed(ACM_TDFriendly friendly, float now)
	{
		vector here = friendly.entity.GetOrigin();

		// First ever sample. Establishes the baseline and reports nothing, rather than reporting the
		// distance from the world origin as a speed.
		if (friendly.lastStatusTime < 0)
		{
			friendly.lastStatusPos = here;
			friendly.lastStatusTime = now;
			friendly.speed = 0;
			return;
		}

		// PREVIOUS position captured before the field is overwritten. Advancing the baseline first
		// and measuring afterwards compares the new position against itself and reports zero speed
		// forever, which looks exactly like a squad that never moves.
		vector previous = friendly.lastStatusPos;
		float elapsed = now - friendly.lastStatusTime;

		friendly.lastStatusPos = here;
		friendly.lastStatusTime = now;

		if (elapsed <= 0)
			return;

		float speed = vector.Distance(here, previous) / elapsed;

		if (speed > TELEPORT_SPEED_MS)
		{
			friendly.speed = 0;
			return;
		}

		friendly.speed = speed;
	}

	//------------------------------------------------------------------------------------------------
	// One line per candidate per second, covering every step that can silently drop a
	// friendly before it reaches the renderer: what faction it resolved to, whether that faction is
	// playable (the civilian test), and what affiliation came out the other end.
	//------------------------------------------------------------------------------------------------
	protected void LogCandidate(IEntity candidate, ACM_TDAffiliation affiliation, float distance, IEntity localEntity)
	{
		Faction targetFaction = GetTargetFaction(candidate);

		string excluded = "no";
		if (targetFaction && IsCivilianFaction(targetFaction.GetFactionKey()))
			excluded = "YES";

		int playerId = -1;
		PlayerManager playerManager = GetGame().GetPlayerManager();
		if (playerManager)
			playerId = playerManager.GetPlayerIdFromControlledEntity(candidate);

		Print(string.Format("[ACM IFF] cand faction=%1 iffExcluded=%2 affiliation=%3 dist=%4m playerId=%5",
			DebugFactionKey(targetFaction),
			excluded,
			DebugAffiliationName(affiliation),
			Math.Round(distance),
			playerId), LogLevel.DEBUG);
	}

	//------------------------------------------------------------------------------------------------
	// One-shot diagnostic for a silent misconfiguration that is otherwise very hard to spot.
	//
	// Everything still draws without the registry - markers, names, inferred role colours - so the
	// only visible symptom of the component being absent is that no player's DECLARED role ever
	// takes effect and the Tactical Role entry never appears in the radial menu. That reads as
	// "role selection is broken" and sends you looking in the role code, when the actual cause is
	// that the running scenario's game mode prefab does not carry ACM_TacticalRoleComponent.
	//------------------------------------------------------------------------------------------------
	protected void WarnOnceIfNoRoleRegistry()
	{
		if (m_bRoleRegistryWarned || ACM_TacticalRoleComponent.HasRegistry())
			return;

		m_bRoleRegistryWarned = true;

		Print("[ACM TD] No ACM_TacticalRoleComponent on the active game mode. Friendly IFF is running on inferred roles only - declared roles cannot be stored or replicated, and the Tactical Role menu entry will not appear. Add ACM_TacticalRoleComponent to the game mode prefab your scenario actually uses.", LogLevel.WARNING);
	}

	//------------------------------------------------------------------------------------------------
	// Label text by display density.
	//
	//   MINIMAL    MED
	//   STANDARD   SMITH  MED - 42m
	//   DETAILED   SMITH  MEDIC - 42m  ALPHA 1-2
	//
	// Density is not cosmetic. Twelve friendlies at STANDARD is readable; the same twelve at
	// DETAILED is a wall of text across the middle of the display.
	//------------------------------------------------------------------------------------------------
	protected string BuildFriendlyLabel(ACM_TDFriendly friendly, ACM_TacticalRolePreset preset)
	{
		ACM_TDIffDetail detail = m_Provider.GetIffDetail();

		string abbreviation = preset.m_sAbbreviation;

		if (detail == ACM_TDIffDetail.MINIMAL)
			return abbreviation;

		string name;
		PlayerManager playerManager = GetGame().GetPlayerManager();
		if (playerManager && friendly.playerId > 0)
			name = ACM_PlayerNames.GetDisplayName(friendly.playerId);

		string roleText = abbreviation;
		if (detail == ACM_TDIffDetail.DETAILED)
			roleText = preset.m_sDisplayName;

		string line = name;

		if (!roleText.IsEmpty())
		{
			if (line.IsEmpty())
				line = roleText;
			else
				line = line + "  " + roleText;
		}

		line = line + string.Format(" - %1m", Math.Round(friendly.distance));

		if (detail != ACM_TDIffDetail.DETAILED)
			return line;

		string group = ResolveGroupName(friendly.playerId);
		if (!group.IsEmpty())
			line = line + "  " + group;

		return line;
	}

	//------------------------------------------------------------------------------------------------
	protected string ResolveGroupName(int playerId)
	{
		if (playerId <= 0)
			return string.Empty;

		SCR_GroupsManagerComponent groups = SCR_GroupsManagerComponent.GetInstance();
		if (!groups)
			return string.Empty;

		SCR_AIGroup group = groups.GetPlayerGroup(playerId);
		if (!group)
			return string.Empty;

		string custom = group.GetCustomName();
		if (!custom.IsEmpty())
			return custom;

		return string.Format("GRP %1", group.GetGroupID());
	}

	//------------------------------------------------------------------------------------------------
	// Highest role priority last, so leaders and specialists land on top when markers overlap and
	// survive the cap when more friendlies are in range than may be drawn.
	//------------------------------------------------------------------------------------------------
	protected void SortFriendliesByPriority(array<ref ACM_TDFriendly> friendlies)
	{
		int count = friendlies.Count();

		for (int i = 1; i < count; i++)
		{
			ref ACM_TDFriendly key = friendlies[i];
			int j = i - 1;

			while (j >= 0 && friendlies[j].priority < key.priority)
			{
				friendlies[j + 1] = friendlies[j];
				j--;
			}

			friendlies[j + 1] = key;
		}
	}

	//------------------------------------------------------------------------------------------------
	// Friendly render pass.
	//
	// Current position, one projection, a glyph and a label. No stored position, because a friendly
	// is never remembered - if the sensor cannot see them this scan they simply are not in the list.
	//------------------------------------------------------------------------------------------------
	protected int DrawFriendly(ACM_TDFriendly friendly, WorkspaceWidget workspace, BaseWorld world, int labelIndex)
	{
		if (!m_Provider.IsFriendlyIffEnabled())
			return labelIndex;

		if (!friendly || !friendly.entity)
			return labelIndex;

		vector markerPos = friendly.entity.GetOrigin() + Vector(0, m_Provider.GetFriendlyMarkerRise(), 0);

		m_iDiagEntering++;

		vector screen = workspace.ProjWorldToScreen(markerPos, world, m_iProjCamera);
		if (screen[2] < NEAR_DEPTH)
			return labelIndex;

		m_iDiagProjected++;

		// Same scope clip as ProjectBodyBox: while looking through a magnified optic this point was
		// projected by the scope camera, so it is only valid inside the glass.
		if (m_bScopeProj && !IsInsideScope(screen))
			return labelIndex;

		float centerX = workspace.DPIScale(screen[0]);
		float centerY = workspace.DPIScale(screen[1]);

		// Missing this was why friendlies never appeared in the feed: projected through the right
		// camera but left in main-view coordinates, so every glyph landed outside the panel and
		// was clipped away.
		if (m_bProjToPanel)
			MapToPanel(workspace, centerX, centerY);

		float maskFactor = MaskFactor(workspace, centerX, centerY);

		if (ACM_TD_DEBUG && m_bTraceFriendlyDraw)
			Print(string.Format("[ACM IFF] draw '%1' screen=(%2,%3) mask=%4 color=%5", friendly.labelText, Math.Round(centerX), Math.Round(centerY), maskFactor, friendly.packedColor), LogLevel.DEBUG);

		if (maskFactor <= 0)
			return labelIndex;

		m_iDiagDrawable++;

		if (!m_bDiagHaveFirst)
		{
			m_bDiagHaveFirst = true;
			m_fDiagFirstX = centerX;
			m_fDiagFirstY = centerY;

			// The alpha every mark colour is multiplied by. drawable only requires this to be
			// above zero, so a mark can be counted, submitted and on-screen while being drawn
			// effectively invisible. If this reads ~1 before a menu and near 0 after one, the
			// marks are being faded out rather than not drawn.
			m_fDiagFirstMask = maskFactor;
		}

		int color = friendly.packedColor;
		if (maskFactor < 1.0)
			color = FadeColor(color, maskFactor);

		float size = m_Provider.GetFriendlyMarkerSize();

		DrawFriendlyOutline(friendly, maskFactor);
		AddRoleGlyphCommand(centerX, centerY, size, color);

		return DrawFriendlyLabel(friendly, workspace, centerX, centerY, size, labelIndex, maskFactor);
	}

	//------------------------------------------------------------------------------------------------
	// Friendly silhouette, in the role colour.
	//
	// The one place this system points the engine outliner at something the wearer cannot
	// necessarily see - and it is deliberate. The hostile outline is gated on confirmed line of
	// sight because drawing an enemy through a wall is a wallhack; drawing your own squad through a
	// wall is what IFF is FOR, and it is the difference between knowing a shape at the end of the
	// corridor is Thomas and shooting him. It also costs nothing: a friendly's position is known
	// because they are on your side, not detected, so there is no raycast here any more than
	// anywhere else on this path.
	//
	// Range-limited separately from the marker. A marker at 400m is a useful dot on the display; a
	// silhouette at 400m is a smear of colour on a hillside, and a whole platoon of them is noise.
	//------------------------------------------------------------------------------------------------
	// How long a highlight survives without being refreshed.
	//
	// Brightness is NOT here - it is a slider on the device (GetScopedHighlightEmissive), because
	// the emissive scale is not documented anywhere I can find and the only honest way to land on
	// a number is to look through the glass and turn the dial.
	//
	// The hold is deliberately short. It is refreshed on every draw pass, so it only matters when
	// the draw pass STOPS: half a second later the character goes dark on its own, without anything
	// having to remember to clear it. That is the primary defence against a permanently glowing
	// enemy, and it costs one float assignment per contact per frame.
	protected static const float HIGHLIGHT_HOLD_SECONDS = 0.5;

	//------------------------------------------------------------------------------------------------
	// Light a character through its own ACM_ScopedHighlightComponent.
	//
	// Returns FALSE on purpose while this is being proven, even when the highlight was applied. The
	// corner box is a known-good mark that lands on the magnified body, and keeping it on screen
	// alongside the glow is what makes the test readable: the box says where the body is, so if the
	// glow is somewhere else - or nowhere at all - that is visible at a glance instead of being
	// guessed at from a log. Once the glow is confirmed, this returns highlight.Light(...) directly
	// and the box goes away.
	//------------------------------------------------------------------------------------------------
	protected bool LightScopedHighlight(IEntity entity, Color color, float maskFactor)
	{
		if (!entity || !color || maskFactor <= 0)
			return false;

		ACM_ScopedHighlightComponent highlight = ACM_ScopedHighlightComponent.Cast(entity.FindComponent(ACM_ScopedHighlightComponent));

		bool lit = false;
		if (highlight)
			lit = highlight.Light(color.PackToInt(), m_Provider.GetScopedHighlightEmissive(), HIGHLIGHT_HOLD_SECONDS);

		// TEMPORARY, once a second. Three separate failures look identical on screen - the component
		// is not on the prefab, the component is there but the character has no material instance, or
		// both are fine and the emissive simply does not show. This tells them apart.
		float probeNow = System.GetTickCount() / 1000.0;
		if (probeNow >= m_fNextScopeProbe)
		{
			m_fNextScopeProbe = probeNow + 1.0;

			// Diagnostic fields FIRST. entity.ToString() runs to a couple of hundred characters and
			// pushed everything that mattered off the right edge of the console last time.
			string d = "[ACM SCOPE] highlight component=";
			d += (highlight != null).ToString();
			d += " mats=";
			if (highlight)
				d += highlight.GetMaterialCount().ToString();
			else
				d += "0";
			d += " lit=";
			d += lit.ToString();
			d += "  ";
			d += entity.ToString();

			Print(d, LogLevel.DEBUG);
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected void DrawFriendlyOutline(ACM_TDFriendly friendly, float maskFactor)
	{
		if (!m_Provider.OutlinesFriendlies() || !friendly.entity || !friendly.color)
			return;

		// Same reason as the hostile silhouette: a main-view pass cannot draw into the scope image.
		// The mesh highlight can, because it changes the model rather than the frame, so the friendly
		// is lit that way instead. Diamond and label are unaffected either way - both project through
		// the scope camera already.
		if (m_bScopeProj)
		{
			LightScopedHighlight(friendly.entity, friendly.color, maskFactor);
			return;
		}

		if (friendly.distance > m_Provider.GetFriendlyOutlineRange())
			return;

		ChimeraWorld world = ChimeraWorld.CastFrom(GetGame().GetWorld());
		if (!world)
			return;

		// Faded by the display mask exactly like the markers, so a friendly walking off the edge of
		// the usable display does not leave a silhouette glowing in the black beyond it.
		world.OutlineEntity(
			friendly.entity,
			friendly.color,
			m_Provider.GetFriendlyOutlineThroughWall() * maskFactor,
			m_Provider.GetFriendlyOutlineFill() * maskFactor,
			true);
	}

	//------------------------------------------------------------------------------------------------
	// The role glyph: a hollow diamond in the role colour.
	//
	// Hollow rather than filled so it reads as a symbol rather than as a hostile marker, and a
	// diamond rather than a bitmap because canvas draw commands cannot render an imageset - that
	// would need a pooled ImageWidget per marker, which is a second rendering system for a single
	// visual. Colour and abbreviation already give two independent identification channels.
	//
	// Real per-role symbols - a cross for the medic, a command symbol for a squad leader - can be
	// layered on later by reading ACM_TacticalRolePreset.m_sIconName, which already exists and is
	// deliberately unused. Nothing about the role, network or catalog architecture changes when
	// they arrive; only this method does.
	//------------------------------------------------------------------------------------------------
	protected void AddRoleGlyphCommand(float centerX, float centerY, float size, int color)
	{
		float width = m_Provider.GetLineWidth();

		AddLine(centerX, centerY - size, centerX + size, centerY, color, width);
		AddLine(centerX + size, centerY, centerX, centerY + size, color, width);
		AddLine(centerX, centerY + size, centerX - size, centerY, color, width);
		AddLine(centerX - size, centerY, centerX, centerY - size, color, width);
	}

	//------------------------------------------------------------------------------------------------
	// Label to the right of the glyph rather than above it, so the marker reads as one horizontal
	// unit - a symbol followed by who it is - and a column of squadmates does not interleave.
	//------------------------------------------------------------------------------------------------
	protected int DrawFriendlyLabel(ACM_TDFriendly friendly, WorkspaceWidget workspace, float centerX, float centerY, float size, int labelIndex, float visibility)
	{
		if (friendly.labelText.IsEmpty() || labelIndex >= m_aLabels.Count())
			return labelIndex;

		TextWidget label = m_aLabels[labelIndex];
		if (!label)
			return labelIndex;

		label.SetText(friendly.labelText);

		int packed = FadeColor(friendly.packedColor, visibility);
		label.SetColor(Color.FromRGBA(
			(packed >> 16) & 0xFF,
			(packed >> 8) & 0xFF,
			packed & 0xFF,
			(packed >> 24) & 0xFF));

		float textWidth, textHeight;
		label.GetTextSize(textWidth, textHeight);

		float refCenterX = workspace.DPIUnscale(centerX);
		float refCenterY = workspace.DPIUnscale(centerY);
		float refSize = workspace.DPIUnscale(size);

		float x = refCenterX + refSize + m_Provider.GetLabelOffset();
		float y = refCenterY - (textHeight * 0.5);

		float refScreenW = workspace.DPIUnscale(workspace.GetWidth());
		float refScreenH = workspace.DPIUnscale(workspace.GetHeight());

		x = Math.Clamp(x, 0, Math.Max(0, refScreenW - textWidth));
		y = Math.Clamp(y, 0, Math.Max(0, refScreenH - textHeight));

		FrameSlot.SetPos(label, x, y);
		label.SetVisible(true);

		return labelIndex + 1;
	}

	//------------------------------------------------------------------------------------------------
	// Overlay lifecycle
	//
	// This root belongs to the CONTACT LAYER and to nothing else. It is created on demand and its
	// widgets are torn down whenever the device stops drawing.
	//
	// THE UAV PiP MUST NOT BE PARENTED TO IT.
	//
	// That is not a style preference. The contact overlay is destroyed and rebuilt on ordinary
	// events - a menu opening, the goggles being switched off, a respawn - and anything parented
	// beneath it dies with it. A video feed hanging off this root would therefore be killed by a
	// contact-layer teardown that has nothing to do with the drone, and re-created blank when the
	// overlay came back, dropping the render target mid-flight.
	//
	// The drone feed gets its own component, its own layout root and its own lifetime:
	//
	//   sensor -> contact lifecycle -> HUD      (information; this file)
	//   drone camera -> PiP / video component   (video; entirely separate)
	//
	// The two may sit side by side on screen and must not share ownership. Hiding the PiP has to
	// stop the render target outright rather than merely hiding a widget, which is another reason
	// its lifetime cannot be tied to this one.
	//------------------------------------------------------------------------------------------------
	//------------------------------------------------------------------------------------------------
	// Drop the overlay widgets so the next EnsureOverlay builds them again.
	//
	// The label pool is cleared with them: those TextWidgets are children of the root and die with
	// it, and keeping stale handles would leave the pool pointing at widgets that no longer exist.
	//------------------------------------------------------------------------------------------------
	//------------------------------------------------------------------------------------------------
	protected void EnsureOverlay()
	{
		if (m_wRoot)
			return;

		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if (!workspace)
			return;

		// BUILT INSIDE THE GAME'S HUD LAYER, NOT LOOSE ON THE WORKSPACE.
		//
		// A workspace-root overlay is not owned by anything that understands menus. The heartbeat
		// showed this root reading null on the first pass after every menu - the widget is torn
		// down and rebuilt each time - and a rebuilt one submits commands to a valid canvas at
		// valid coordinates while presenting nothing.
		//
		// The HUD layer is where the engine's own screen-space UI lives: nametags and waypoints
		// are SCR_InfoDisplays parented here, and they come through menus intact because the HUD
		// manager owns their lifecycle instead of leaving it to chance. Parenting into it is the
		// smallest step that puts this overlay under the same ownership.
		//
		// Falls back to the workspace root when there is no HUD manager - the Workbench preview
		// and any context without a player controller - so this cannot make things worse than the
		// behaviour it replaces.
		Widget hudRoot = null;

		SCR_HUDManagerComponent hud = SCR_HUDManagerComponent.GetHUDManager();
		if (hud)
			hudRoot = hud.GetHUDRootWidget();

		if (hudRoot)
			m_wRoot = workspace.CreateWidgets(OVERLAY_LAYOUT, hudRoot);
		else
			m_wRoot = workspace.CreateWidgets(OVERLAY_LAYOUT);

		if (!m_wRoot)
		{
			// Qualified id did not resolve - try the bare path before declaring failure.
			m_wRoot = workspace.CreateWidgets(OVERLAY_LAYOUT_FALLBACK);

			if (m_wRoot)
				Print("[ACM TargetDetection] Overlay layout resolved by path, not by id. The resource database has not indexed its .meta - open the addon in Workbench to fix properly.", LogLevel.WARNING);
		}

		if (!m_wRoot)
		{
			Print("[ACM TargetDetection] Overlay layout failed to load. Open the addon in Workbench once so UI/layouts/HUD/ACM_TargetDetectionOverlay.layout is registered in the resource database.", LogLevel.WARNING);
			return;
		}

		m_wCanvas = CanvasWidget.Cast(m_wRoot.FindAnyWidget("ACM_TDCanvas"));

		// TEMPORARY - how many times this overlay has been built this session.
		//
		// The heartbeat shows root=0 on the first pass after every menu, meaning the widget handle
		// is null by then and this rebuilds it. The visor overlay is never rebuilt and recovers
		// fine, so this counter answers whether we are quietly stacking a fresh overlay per menu -
		// and if the count climbs while the marks stay invisible, the canvas being drawn into is
		// not the one on screen.
		if (ACM_OVERLAY_DIAG)
		{
			m_iOverlayBuilds++;

			string b = "[ACM IVAS] overlay BUILT #";
			b += m_iOverlayBuilds.ToString();
			b += "  canvasFound=";
			b += (m_wCanvas != null).ToString();
			b += "  parent=";
			if (m_wRoot.GetParent())
				b += "hud";
			else
				b += "workspace-root";

			Print(b, LogLevel.DEBUG);
		}

		m_aLabels.Clear();
		for (int i = 0; i < LABEL_POOL_SIZE; i++)
		{
			TextWidget label = TextWidget.Cast(m_wRoot.FindAnyWidget("ACM_TDLabel" + i.ToString()));
			m_aLabels.Insert(label);
		}
	}

	//------------------------------------------------------------------------------------------------
	// Submitting an empty command list is not optional - a CanvasWidget keeps rendering
	// whatever was last handed to it, so simply stopping the draw loop would freeze the
	// outlines on screen.
	//------------------------------------------------------------------------------------------------
	protected void ClearOverlay()
	{
		// Alternates like every other submission: handing the canvas the instance it already holds
		// is exactly the state this overlay does not recover from.
		array<ref CanvasWidgetCommand> cleared = FlipDrawBuffer();
		m_aActiveCommands = cleared;

		if (m_wCanvas)
			m_wCanvas.SetDrawCommands(cleared);

		// The feed canvas holds its own list and would keep drawing the last one.
		m_aFeedDrawCommands.Clear();

		ACM_RemoteFeedRenderer feed = ACM_RemoteFeedRenderer.GetInstance();
		if (feed && feed.GetFeedCanvas())
			feed.GetFeedCanvas().SetDrawCommands(m_aFeedDrawCommands);

		foreach (TextWidget label : m_aLabels)
		{
			if (label)
				label.SetVisible(false);
		}
	}
}
