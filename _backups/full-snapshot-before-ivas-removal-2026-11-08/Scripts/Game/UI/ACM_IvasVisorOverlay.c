//------------------------------------------------------------------------------------------------
// ACM_IvasVisorOverlay.c
// The visor the wearer looks through while the IVAS is up.
//
// Its own root and its own lifetime, like the boot splash and the remote feed. NOT parented to the
// contact overlay, which is created and destroyed on menu opens, respawns and device switches -
// anything hanging beneath that would die with it and come back blank.
//
// Purely local. No signal, no RPC, no replicated state. It draws on one screen and nobody else ever
// hears about it.
//
//------------------------------------------------------------------------------------------------
// TWO CLOCKS, AND THE SPLIT IS DELIBERATE
//
// RibbonTick runs every frame. It has to: it is drawing a compass, and a compass that lags the
// wearer's own head by half a second is worse than no compass. What it does per frame is arithmetic
// on positions that were resolved elsewhere - one subtraction and one atan2 per mark - and nothing
// else. It resolves no names, builds no strings and touches no inventory.
//
// Refresh runs twice a second. Everything on it - a callsign, a role, an ammo count, a grid square,
// five lines of log text - changes on the order of once a second at most, and rebuilding those
// strings sixty times a second to display the same characters would be the single most wasteful
// thing in the HUD path.
//
// The rule for adding to this file: if it needs a string built, it goes on Refresh.
//------------------------------------------------------------------------------------------------
// WHAT THIS DISPLAY IS ALLOWED TO SAY
//
// Every readout below is backed by something the game already knows and this system already
// resolved. There is no invented gauge anywhere in here - no battery, no signal bar, no wind, no
// range-to-target on something that was never ranged. Those were all considered and left out,
// because a confident-looking readout with nothing behind it is worse than a blank corner: the
// wearer cannot tell the difference, and will act on it.
//
// The bearing ribbon obeys the same position rule as the markers - live only while the contact's
// own sight test says live, frozen otherwise, and drawn broken when frozen so the compass never
// implies tracking the brackets are refusing to claim.
//------------------------------------------------------------------------------------------------
class ACM_IvasVisorOverlay
{
	// Fully qualified now that Workbench has indexed the .meta and issued an id.
	//
	// A path-only reference resolves, but a registered id is what the resource database actually
	// wants - and this project has already been round the loop of a layout with no .meta logging
	// "Wrong GUID for resource {0000000000000000}" on every load.
	protected static const ResourceName VISOR_LAYOUT = "{30D03FD1B8942835}UI/layouts/HUD/ACM_IvasVisor.layout";

	protected static const int REFRESH_MS = 500;

	//------------------------------------------------------------------------------------------------
	// Ribbon geometry, in LAYOUT REFERENCE UNITS - the same units the .layout is authored in and the
	// same ones FrameSlot reads. Canvas draw commands are in scaled pixels, so everything that
	// becomes a line goes through DPIScale on the way and everything that becomes a widget position
	// does not. Mixing the two is the classic way to get a HUD that is correct on one monitor.
	//------------------------------------------------------------------------------------------------
	protected static const float RIBBON_W = 900.0;
	protected static const float RIBBON_H = 44.0;

	// Baseline the ticks and marks stand on.
	//
	// Deliberately NOT at the bottom of the canvas. The centre caret hangs below this line, and a
	// canvas clips its own draw commands - a baseline flush with the bottom edge would shave the
	// tips off the one marker on the strip that tells the wearer where they are pointing.
	protected static const float RIBBON_BASE_Y = 36.0;

	// Tick tops. Minor every MINOR_STEP degrees, major every MAJOR_STEP with a heading label.
	protected static const float RIBBON_MINOR_TOP = 30.0;
	protected static const float RIBBON_MAJOR_TOP = 24.0;
	protected static const int RIBBON_MINOR_STEP = 15;
	protected static const int RIBBON_MAJOR_STEP = 45;

	// How tall a full-height contact mark stands above the baseline.
	protected static const float RIBBON_MARK_H = 16.0;

	// Sides in the hostile dot. Eight is the point where a polygon stops reading as a polygon at
	// this size and starts reading as a dot; more is vertices nobody can see.
	protected static const int DOT_SIDES = 8;

	// How far in the inner ring of a data-link mark sits, as a fraction of the outer radius.
	protected static const float LINK_INNER = 0.45;

	// The whole circle, mapped across the strip. Not a forward-facing arc, on purpose: the point of
	// the ribbon is the contact you are NOT looking at, and a 120-degree window hides exactly those.
	protected static const float RIBBON_SPAN_DEG = 360.0;

	protected static const int RIBBON_LABELS = 8;
	protected static const int SQUAD_ROWS = 16;

	//! Longest callsign the roster row will print before truncating. See BuildSquadLine.
	protected static const int NAME_MAX_CHARS = 12;

	protected static Widget s_wRoot;
	protected static TextWidget s_wId;
	protected static TextWidget s_wRole;
	protected static TextWidget s_wMode;
	protected static TextWidget s_wStatus;

	protected static CanvasWidget s_wRibbonCanvas;
	protected static TextWidget s_wHeading;
	protected static ref array<TextWidget> s_aRibbonLabels = {};

	protected static TextWidget s_wSquadHeader;

	// The dark plate behind the squad rows.
	//
	// Hidden with its content. The other three plates sit behind readouts that always say something,
	// but an empty squad panel would be a dark rectangle on the visor announcing nothing at all.
	protected static Widget s_wSquadPanel;

	// Whether the squad plate is currently up.
	//
	// Read by the remote feed, which docks to a screen corner and has to know that the left edge is
	// occupied. Kept as a flag rather than left to the widget's own visibility so that a CHANGE can
	// be detected - the feed only needs re-placing when the strip appears or goes away, not on every
	// refresh tick that finds the same squad still there.
	protected static bool s_bSquadShown;
	protected static ref array<TextWidget> s_aSquadRows = {};

	// Look-at casualty panel.
	protected static Widget s_wCasualtyPanel;
	protected static TextWidget s_wCasualtyName;
	protected static TextWidget s_wCasualtyState;
	protected static ref array<TextWidget> s_aCasualtyRows = {};

	protected static const int CASUALTY_ROWS = 8;

	// The callout has no geometry here on purpose.
	//
	// It used to be positioned from script, tracking the casualty's projected screen point. That is
	// gone: a panel that follows a body follows every twitch of a settling ragdoll and every sway of
	// the wearer's own head, and no amount of smoothing makes a block of clinical text readable while
	// it drifts. It now lives at a fixed spot on the right of the visor, placed in
	// ACM_IvasVisor.layout, so the wearer's eye learns one location and goes straight to it.
	protected static ref array<TextWidget> s_aLogRows = {};

	protected static TextWidget s_wWeaponName;
	protected static TextWidget s_wWeaponAmmo;
	protected static TextWidget s_wGrid;

	// Owned for the lifetime of the overlay and cleared in place rather than reallocated. A
	// CanvasWidget renders whatever list it was last handed, so handing it a new array every frame
	// would mean the previous one is live right up until the swap.
	protected static ref array<ref CanvasWidgetCommand> s_aRibbonCommands = {};

	// Packed once when the widgets are built. Color.FromRGBA allocates, and these two are constant -
	// building them inside a sixty-times-a-second tick would be two throwaway objects per frame for
	// a value that never changes.
	protected static int s_iScaleColor;
	protected static int s_iCaretColor;

	protected static IEntity s_Device;

	// The last device this visor came up on, kept ACROSS Hide().
	//
	// s_Device is nulled on the way down, so it cannot answer "is this a different unit" on the way
	// back up - lowering and raising the same IVAS would look exactly like swapping to a new one and
	// would wipe the event log every time the wearer flipped the visor.
	protected static IEntity s_LastDevice;

	//------------------------------------------------------------------------------------------------
	static void Show(IEntity device)
	{
		if (!device || !GetGame() || !GetGame().GetWorkspace())
			return;

		// A different device is a different session. The log belongs to the IVAS that recorded it,
		// and a fresh unit coming up showing the last one's contacts would be a lie about where
		// that information came from.
		if (s_LastDevice && s_LastDevice != device)
			ACM_IvasEventLog.Clear();

		s_Device = device;
		s_LastDevice = device;

		if (!CreateWidgets())
			return;

		GetGame().GetCallqueue().Remove(Refresh);
		GetGame().GetCallqueue().Remove(RibbonTick);

		GetGame().GetCallqueue().CallLater(Refresh, REFRESH_MS, true);
		GetGame().GetCallqueue().CallLater(RibbonTick, 0, true);

		Refresh();
	}

	//------------------------------------------------------------------------------------------------
	static void Hide()
	{
		s_Device = null;

		if (GetGame() && GetGame().GetCallqueue())
		{
			GetGame().GetCallqueue().Remove(Refresh);
			GetGame().GetCallqueue().Remove(RibbonTick);
		}

		s_wId = null;
		s_wRole = null;
		s_wMode = null;
		s_wStatus = null;

		s_wRibbonCanvas = null;
		s_wHeading = null;
		s_aRibbonLabels.Clear();

		s_wSquadHeader = null;
		s_wSquadPanel = null;
		s_bSquadShown = false;
		s_aSquadRows.Clear();
		s_aLogRows.Clear();

		s_wWeaponName = null;
		s_wWeaponAmmo = null;
		s_wGrid = null;

		s_aRibbonCommands.Clear();

		if (s_wRoot)
		{
			s_wRoot.RemoveFromHierarchy();
			s_wRoot = null;
		}
	}

	//------------------------------------------------------------------------------------------------
	static bool IsShown()
	{
		return s_wRoot != null;
	}

	//------------------------------------------------------------------------------------------------
	// Is the squad strip occupying the left edge right now?
	//
	// Asked by the remote feed. Answers false the moment the visor is up, so nothing has to remember
	// to clear the flag on teardown.
	//------------------------------------------------------------------------------------------------
	static bool IsSquadStripShown()
	{
		return s_wRoot != null && s_bSquadShown;
	}

	//------------------------------------------------------------------------------------------------
	// THREAT BEARING RIBBON - per frame
	//
	// Marks arrive as world positions from the scan pass and become bearings here, which is why a
	// contact stays put on the compass while the wearer sidesteps rather than sliding a scan behind.
	//
	// Also the single owner of the visor's visibility. Two owners on different clocks fighting over
	// SetVisible is how a HUD ends up flickering when a menu opens.
	//------------------------------------------------------------------------------------------------
	protected static void RibbonTick()
	{
		if (!s_wRoot)
			return;

		// A menu being open is a teardown case, the same as it is for the contact overlay - a visor
		// frame drawn over an open inventory reads as a stuck HUD rather than as immersion.
		if (GetGame().GetMenuManager() && GetGame().GetMenuManager().IsAnyMenuOpen())
		{
			s_wRoot.SetVisible(false);
			BlankRibbon();
			return;
		}

		// No rebuild on menu close: this overlay recovers on its own, and a widget rebuilt after the
		// menu system has cycled its layers does not render at all. See the note in
		// ACM_TargetDetectionSystem.DrawTick.
		s_wRoot.SetVisible(true);

		if (!s_wRibbonCanvas)
			return;

		WorkspaceWidget workspace = GetGame().GetWorkspace();
		BaseWorld world = GetGame().GetWorld();
		if (!workspace || !world)
			return;

		vector camMat[4];
		world.GetCurrentCamera(camMat);
		vector camPos = camMat[3];
		vector camForward = camMat[2];

		float heading = HeadingOf(camForward);

		s_aRibbonCommands.Clear();

		float wPx = workspace.DPIScale(RIBBON_W);
		float basePx = workspace.DPIScale(RIBBON_BASE_Y);
		float minorPx = workspace.DPIScale(RIBBON_MINOR_TOP);
		float majorPx = workspace.DPIScale(RIBBON_MAJOR_TOP);
		float pxPerDeg = wPx / RIBBON_SPAN_DEG;

		// The scale itself. Drawn from the wearer's heading outwards rather than from north, so the
		// ticks sit at whole compass degrees instead of sliding around underneath the marks.
		for (int deg = 0; deg < 360; deg += RIBBON_MINOR_STEP)
		{
			float tickX = (wPx * 0.5) + (NormalizeDelta(deg - heading) * pxPerDeg);

			if (tickX < 0 || tickX > wPx)
				continue;

			float top = minorPx;
			if ((deg % RIBBON_MAJOR_STEP) == 0)
				top = majorPx;

			AddRibbonLine(tickX, top, tickX, basePx, s_iScaleColor, workspace.DPIScale(1.0));
		}

		PositionRibbonLabels(heading);

		// Contacts, non-combatants and squad, all in one flat list the scan pass already ordered.
		ACM_TargetDetectionSystem system = ACM_TargetDetectionSystem.GetInstance();
		if (system && RibbonMarksAllowed(system))
		{
			array<ref ACM_IvasBearingMark> marks = system.GetBearingMarks();

			// The COUNT, never Count(). The array is a pool and everything past the count is a mark
			// from a busier scan that has not been overwritten yet.
			int liveMarks = system.GetBearingMarkCount();

			for (int i = 0; i < liveMarks; i++)
			{
				ACM_IvasBearingMark mark = marks[i];
				if (!mark)
					continue;

				vector delta = mark.worldPos - camPos;
				float bearing = HeadingOf(delta);

				float markX = (wPx * 0.5) + (NormalizeDelta(bearing - heading) * pxPerDeg);

				if (markX < 0 || markX > wPx)
					continue;

				float markTop = basePx - workspace.DPIScale(RIBBON_MARK_H * mark.height);
				float width = workspace.DPIScale(mark.weight);
				float radius = workspace.DPIScale(mark.glyphRadius);

				// Stem starts UNDER the glyph rather than at its centre, so a hollow dot stays
				// hollow instead of having a line drawn up through the middle of it.
				float stemTop = markTop + radius;
				if (stemTop < basePx)
				{
					if (mark.frozen)
						AddBrokenMark(markX, stemTop, basePx, mark.packedColor, width);
					else
						AddRibbonLine(markX, stemTop, markX, basePx, mark.packedColor, width);
				}

				AddMarkGlyph(markX, markTop, radius, mark);
			}
		}

		// Centre caret, last so it sits over everything. This is where the wearer is looking.
		float centreX = wPx * 0.5;
		float caretH = workspace.DPIScale(6.0);
		float caretW = workspace.DPIScale(5.0);
		float caretWidth = workspace.DPIScale(1.5);

		AddRibbonLine(centreX - caretW, basePx + caretH, centreX, basePx, s_iCaretColor, caretWidth);
		AddRibbonLine(centreX, basePx, centreX + caretW, basePx + caretH, s_iCaretColor, caretWidth);

		s_wRibbonCanvas.SetDrawCommands(s_aRibbonCommands);

		if (s_wHeading)
			s_wHeading.SetText(Pad3(heading) + "  " + CardinalOf(heading));
	}

	//------------------------------------------------------------------------------------------------
	// The master switch and the HUD switch both silence the ribbon, because both silence every other
	// marker. A compass that kept reporting contacts after the wearer switched augmentation off
	// would make the master switch a lie.
	//------------------------------------------------------------------------------------------------
	protected static bool RibbonMarksAllowed(ACM_TargetDetectionSystem system)
	{
		ACM_TargetDetectionComponent provider = system.GetActiveProvider();
		if (!provider)
			return false;

		return provider.IsIvasEnabled() && provider.IsHudEnabled();
	}

	//------------------------------------------------------------------------------------------------
	// The glyph at the top of a mark's stem. This is the thing the wearer actually reads.
	//
	// Four states, and every one of them is a fact the contact already carried:
	//
	//   filled dot      the wearer's own sensor has eyes on it right now
	//   hollow dot      the wearer saw it, and this is where it was
	//   double ring     a squadmate's report over the data link, never seen by this device
	//   filled diamond  a friendly, same glyph the main display puts over their head
	//   hollow square   a non-combatant
	//
	// Nothing here decides any of that. If the ribbon and the brackets ever disagree about a
	// contact, the bug is upstream in BuildBearingMarks, not in this method.
	//------------------------------------------------------------------------------------------------
	protected static void AddMarkGlyph(float cx, float cy, float radius, ACM_IvasBearingMark mark)
	{
		float outline = Math.Max(1.0, radius * 0.42);

		if (mark.shape == ACM_EIvasMarkShape.DIAMOND)
		{
			AddMarkShape(cx, cy, radius, mark.packedColor, 4, 0, mark.filled, outline);
			return;
		}

		if (mark.shape == ACM_EIvasMarkShape.SQUARE)
		{
			// A four-gon turned an eighth of a turn is a square. Same builder, flat top.
			AddMarkShape(cx, cy, radius, mark.packedColor, 4, 45.0, mark.filled, outline);
			return;
		}

		AddMarkShape(cx, cy, radius, mark.packedColor, DOT_SIDES, 0, mark.filled, outline);

		// The relay ring. Sits INSIDE the outer one rather than above or beside it, so telling a
		// shared contact apart costs the ribbon no vertical room - and there is none to spare
		// between the mark band and the heading labels.
		if (mark.link)
			AddMarkShape(cx, cy, radius * LINK_INNER, mark.packedColor, DOT_SIDES, 0, false, Math.Max(1.0, outline * 0.7));
	}

	//------------------------------------------------------------------------------------------------
	// One regular polygon, filled or outlined.
	//
	// A dot, a diamond and a square are the same shape with a different vertex count and a different
	// quarter turn, so they are one function rather than three near-copies that can drift apart.
	//
	// Vertex zero is at the TOP and they run clockwise, which is what puts a point up on the diamond
	// and a flat edge up on the square at 45 degrees.
	//------------------------------------------------------------------------------------------------
	protected static void AddMarkShape(float cx, float cy, float radius, int color, int sides, float rotationDeg, bool filled, float width)
	{
		if (sides < 3 || radius <= 0)
			return;

		// The command owns its vertices, and it is built before they exist so that the array it
		// owns is the one being filled. A local holding the vertices and handed over afterwards
		// would work, but this way there is only ever one owner and no question about when the
		// canvas's pointer became valid.
		array<float> verts;

		if (filled)
		{
			PolygonDrawCommand poly = new PolygonDrawCommand();
			poly.m_Vertices = {};
			poly.m_iColor = color;

			verts = poly.m_Vertices;
			BuildRegularPolygon(verts, cx, cy, radius, sides, rotationDeg);

			s_aRibbonCommands.Insert(poly);
			return;
		}

		// m_bShouldEnclose closes the loop back to vertex zero. Without it the ring is a C.
		LineDrawCommand ring = new LineDrawCommand();
		ring.m_Vertices = {};
		ring.m_iColor = color;
		ring.m_fWidth = width;
		ring.m_bShouldEnclose = true;

		verts = ring.m_Vertices;
		BuildRegularPolygon(verts, cx, cy, radius, sides, rotationDeg);

		s_aRibbonCommands.Insert(ring);
	}

	//------------------------------------------------------------------------------------------------
	// Vertex zero at the TOP, running clockwise. That is what puts a point up on the diamond and,
	// with a 45 degree turn, a flat edge up on the square.
	//------------------------------------------------------------------------------------------------
	protected static void BuildRegularPolygon(notnull array<float> verts, float cx, float cy, float radius, int sides, float rotationDeg)
	{
		float offset = rotationDeg * Math.DEG2RAD;

		for (int i = 0; i < sides; i++)
		{
			float angle = offset + ((i * Math.PI2) / sides);

			verts.Insert(cx + (Math.Sin(angle) * radius));
			verts.Insert(cy - (Math.Cos(angle) * radius));
		}
	}

	//------------------------------------------------------------------------------------------------
	// A frozen mark, drawn as two stubs with a gap. The one visual difference on the ribbon between
	// "I can see this" and "this is where it was".
	//------------------------------------------------------------------------------------------------
	protected static void AddBrokenMark(float x, float top, float bottom, int color, float width)
	{
		float span = bottom - top;
		if (span <= 0)
			return;

		float segment = span * 0.38;

		AddRibbonLine(x, top, x, top + segment, color, width);
		AddRibbonLine(x, bottom - segment, x, bottom, color, width);
	}

	//------------------------------------------------------------------------------------------------
	protected static void AddRibbonLine(float x0, float y0, float x1, float y1, int color, float width)
	{
		LineDrawCommand line = new LineDrawCommand();
		line.m_Vertices = { x0, y0, x1, y1 };
		line.m_iColor = color;
		line.m_fWidth = width;

		s_aRibbonCommands.Insert(line);
	}

	//------------------------------------------------------------------------------------------------
	// Cardinal headings slide under the caret with the wearer's turn.
	//
	// FrameSlot positions in reference units rather than the scaled pixels the canvas works in, so
	// this method deliberately does none of its arithmetic in pixels.
	//------------------------------------------------------------------------------------------------
	protected static void PositionRibbonLabels(float heading)
	{
		for (int i = 0; i < s_aRibbonLabels.Count(); i++)
		{
			TextWidget label = s_aRibbonLabels[i];
			if (!label)
				continue;

			float deg = i * RIBBON_MAJOR_STEP;
			float refX = (RIBBON_W * 0.5) + (NormalizeDelta(deg - heading) * (RIBBON_W / RIBBON_SPAN_DEG));

			// Nothing clips a FrameWidget's children, so a label that has wrapped past the end of
			// the strip has to be hidden rather than merely moved.
			if (refX < 0 || refX > RIBBON_W)
			{
				label.SetVisible(false);
				continue;
			}

			label.SetText(CardinalOf(deg));

			// The authored box is 60 wide with the text centred in it, so half a box left of the
			// mark puts the glyph on the mark.
			FrameSlot.SetPos(label, refX - 30.0, 1.0);
			label.SetVisible(true);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static void BlankRibbon()
	{
		s_aRibbonCommands.Clear();

		if (s_wRibbonCanvas)
			s_wRibbonCanvas.SetDrawCommands(s_aRibbonCommands);
	}

	//------------------------------------------------------------------------------------------------
	// Everything that needs a string built. Twice a second.
	//------------------------------------------------------------------------------------------------
	protected static void Refresh()
	{
		if (!s_wRoot)
			return;

		// Visibility belongs to RibbonTick. This pass would otherwise spend its work writing text
		// into a hidden panel, so it simply stops.
		if (GetGame().GetMenuManager() && GetGame().GetMenuManager().IsAnyMenuOpen())
			return;

		if (s_wId)
			s_wId.SetText("ID: " + ResolveCallsign());

		if (s_wRole)
			s_wRole.SetText(ResolveRole());

		if (s_wMode)
			s_wMode.SetText("MODE: " + ResolveMode());

		if (s_wStatus)
			s_wStatus.SetText(ResolveStatus());

		RefreshSquad();
		RefreshCasualtyPanel();
		RefreshLog();
		RefreshWeapon();
		RefreshGrid();
	}

	//------------------------------------------------------------------------------------------------
	// SQUAD STATUS STRIP
	//
	// Reads the SQUAD ROSTER, not the marker store.
	//
	// That distinction is the feature. The marker store answers "which friendlies am I drawing right
	// now", and is shaped by the view cone, the detection range, the IFF mode and the marker cap - a
	// squadmate behind you is not in it. The roster answers "where is my squad", built from the
	// group itself, and has none of those qualifiers: everyone in your group inside the roster
	// radius is on this strip whether or not you can see them and whichever way you are facing.
	//
	// It is still friendlies only. Nothing hostile, unknown or non-combatant can reach this panel -
	// the store it reads has no branch that admits one.
	//------------------------------------------------------------------------------------------------
	//------------------------------------------------------------------------------------------------
	// LOOK-AT CASUALTY PANEL.
	//
	// Raised only when the wearer is actually looking at a wounded friendly, within reach, with the
	// weapon down - the system resolves all of that; this only renders what it hands over. Hidden
	// outright the rest of the time rather than left blank, so it never occupies screen while empty.
	//
	// Shows the findings a medic would want before kneeling: how bad overall, what is bleeding and how
	// fast, and which parts are wrecked. Zones are listed worst first, because the list is capped and
	// the ones that fall off the end should be the ones that matter least.
	//------------------------------------------------------------------------------------------------
	protected static void RefreshCasualtyPanel()
	{
		ACM_TargetDetectionSystem system = ACM_TargetDetectionSystem.GetInstance();

		IEntity casualty = null;
		ACM_VitalsSnapshot v = null;

		if (system)
		{
			casualty = system.GetLookedAtCasualty();
			v = system.GetLookedAtVitals();
		}

		if (!casualty || !v || !v.valid)
		{
			ShowCasualtyPanel(false);
			return;
		}

		if (s_wCasualtyName)
			s_wCasualtyName.SetText(CasualtyName(casualty));

		if (s_wCasualtyState)
		{
			string header = v.StateLabel();

			// Both axes when they differ: an unconscious man still has a triage grade, and it is the
			// grade that says whether you treat him first.
			if (v.state != ACM_EVitalState.HEALTHY && v.severity != ACM_EInjurySeverity.NONE && v.StateLabel() != v.SeverityLabel())
			{
				header += "  /  ";
				header += v.SeverityLabel();
			}

			s_wCasualtyState.SetText(header);
			s_wCasualtyState.SetColor(VitalsColorFor(v));
		}

		int line = 0;

		if (v.hasBlood && line < s_aCasualtyRows.Count())
		{
			string blood = "BLOOD  ";
			blood += Math.Round(v.bloodFraction * 100).ToString();
			blood += "%";

			if (v.hasBleeding && v.bleedRate > 0)
			{
				blood += "     BLEEDING  ";
				blood += v.bleedRate.ToString(-1, 2);
				blood += "/s";
			}

			SetCasualtyRow(line, blood);
			line++;
		}

		if (v.hasHeartRate && line < s_aCasualtyRows.Count())
		{
			string mark = "";
			if (v.derived)
				mark = "~";

			string vitals = "HR  ";
			vitals += mark;
			vitals += v.heartRate.ToString();
			vitals += "     BPM  ";
			vitals += mark;
			vitals += v.breathsPerMinute.ToString();

			if (v.derived)
				vitals += "     (est)";

			SetCasualtyRow(line, vitals);
			line++;
		}

		if (v.injuries.IsEmpty() && line < s_aCasualtyRows.Count())
		{
			SetCasualtyRow(line, "NO LOCALISED WOUNDS");
			line++;
		}

		foreach (ACM_VitalInjury injury : v.injuries)
		{
			if (line >= s_aCasualtyRows.Count())
				break;

			if (!injury)
				continue;

			string wound = injury.zone;
			wound += "   ";
			wound += Math.Round(injury.damage * 100).ToString();
			wound += "%";

			if (injury.bleedRate > 0)
			{
				wound += "   BLEED ";
				wound += injury.bleedRate.ToString(-1, 2);
			}

			SetCasualtyRow(line, wound);
			line++;
		}

		// TREATMENT TIMELINE, newest first.
		//
		// Last, and only with whatever rows the wounds left over. Findings decide what you do next;
		// history explains what you are looking at. When space is short the findings win.
		array<string> meds = {};
		int medCount = ACM_MedicationLog.GetRecent(casualty, meds, s_aCasualtyRows.Count() - line);

		if (medCount > 0 && line < s_aCasualtyRows.Count())
		{
			foreach (string med : meds)
			{
				if (line >= s_aCasualtyRows.Count())
					break;

				SetCasualtyRow(line, "+ " + med);
				line++;
			}
		}

		for (int blank = line; blank < s_aCasualtyRows.Count(); blank++)
		{
			if (s_aCasualtyRows[blank])
				s_aCasualtyRows[blank].SetVisible(false);
		}

		ShowCasualtyPanel(true);
	}

	//------------------------------------------------------------------------------------------------
	//------------------------------------------------------------------------------------------------
	protected static void SetCasualtyRow(int index, string text)
	{
		if (index < 0 || index >= s_aCasualtyRows.Count())
			return;

		TextWidget row = s_aCasualtyRows[index];
		if (!row)
			return;

		row.SetText(text);
		row.SetVisible(true);
	}

	//------------------------------------------------------------------------------------------------
	protected static void ShowCasualtyPanel(bool show)
	{
		if (s_wCasualtyPanel)
			s_wCasualtyPanel.SetVisible(show);

		if (s_wCasualtyName)
			s_wCasualtyName.SetVisible(show);

		if (s_wCasualtyState)
			s_wCasualtyState.SetVisible(show);

		if (show)
			return;

		foreach (TextWidget row : s_aCasualtyRows)
		{
			if (row)
				row.SetVisible(false);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Name for whoever the ray struck. Players resolve by id; anyone else is just a casualty.
	protected static string CasualtyName(IEntity casualty)
	{
		PlayerManager players = GetGame().GetPlayerManager();
		if (players)
		{
			int playerId = players.GetPlayerIdFromControlledEntity(casualty);
			if (playerId > 0)
			{
				string resolved = ACM_PlayerNames.GetDisplayName(playerId);
				if (!resolved.IsEmpty())
				{
					resolved.ToUpper();
					return resolved;
				}
			}
		}

		return "CASUALTY";
	}

	//------------------------------------------------------------------------------------------------
	protected static void RefreshSquad()
	{
		int shown = 0;

		ACM_TargetDetectionSystem system = ACM_TargetDetectionSystem.GetInstance();
		vector viewer = ViewerOrigin();

		int rosterSize = 0;

		// SQUAD or NEARBY. The roster falls back to friendlies in range when the wearer has no
		// group, and the header has to say which it is - presenting nearby strangers as a squad
		// would be the panel claiming a chain of command that does not exist.
		string heading = "SQUAD";

		if (system && viewer != vector.Zero)
		{
			array<ref ACM_TDFriendly> friendlies = system.GetSquad();
			rosterSize = friendlies.Count();
			heading = system.GetSquadHeading();

			foreach (ACM_TDFriendly friendly : friendlies)
			{
				if (shown >= s_aSquadRows.Count())
					break;

				if (!friendly || !friendly.entity)
					continue;

				// TWO ROWS PER PERSON. The identity line, then the vitals line beneath it.
				//
				// Both are claimed up front so a casualty never renders with their vitals line orphaned onto
				// the next person, or clipped off the bottom of the strip entirely.
				if (shown + 1 >= s_aSquadRows.Count())
					break;

				TextWidget row = s_aSquadRows[shown];
				TextWidget vitalsRow = s_aSquadRows[shown + 1];
				if (!row || !vitalsRow)
					continue;

				row.SetText(BuildSquadLine(friendly, viewer));

				// The identity line keeps the role colour; the state colour goes on the vitals line so a
				// casualty reads as red without destroying the IFF colour that says who they are.
				if (friendly.color)
					row.SetColor(friendly.color);

				row.SetVisible(true);

				vitalsRow.SetText(BuildVitalsLine(friendly));
				vitalsRow.SetColor(VitalsColor(friendly));
				vitalsRow.SetVisible(true);

				shown += 2;
			}
		}

		for (int i = shown; i < s_aSquadRows.Count(); i++)
		{
			if (s_aSquadRows[i])
				s_aSquadRows[i].SetVisible(false);
		}

		if (s_wSquadHeader)
		{
			// shown counts ROWS and each person occupies two, so the header divides. Reporting the
			// row count would tell the wearer their squad is twice its real size.
			int listed = shown / 2;

			if (listed == 0)
			{
				s_wSquadHeader.SetVisible(false);
			}
			else
			{
				// Says so when the strip is not showing everyone. The roster is sorted by role
				// priority so the rows that survive are the leaders and specialists - but a header
				// reading "SQUAD 8" while twelve people are in range would be the panel quietly
				// lying about the size of the squad.
				if (rosterSize > listed)
					s_wSquadHeader.SetText(string.Format("%1  %2 / %3", heading, listed, rosterSize));
				else
					s_wSquadHeader.SetText(string.Format("%1  %2", heading, listed));

				s_wSquadHeader.SetVisible(true);
			}
		}

		if (s_wSquadPanel)
			s_wSquadPanel.SetVisible(shown > 0);

		// Only on the edge. A squad that was there last tick and is still there this tick has not
		// changed the shape of the screen and must not cost the feed a re-placement twice a second.
		bool squadShown = (shown > 0);
		if (squadShown != s_bSquadShown)
		{
			s_bSquadShown = squadShown;

			ACM_RemoteFeedRenderer feed = ACM_RemoteFeedRenderer.GetInstance();
			if (feed)
				feed.RefreshPlacement();
		}
	}

	//------------------------------------------------------------------------------------------------
	//------------------------------------------------------------------------------------------------
	// The vitals line: pulse, respiration, bleed rate.
	//
	// A tilde marks an ESTIMATE. Neither vanilla Reforger nor ACE-Anvil models a heart rate - there
	// is genuinely nothing to read - so these are inferred from blood volume, bleed rate and life
	// state. A number a medic may act on has to say whether it was measured or reasoned, and the
	// moment a medical mod does supply real values the provider clears the flag and the tilde goes.
	//
	// Bleed rate is only shown when there is one. A steady row of "BLEED 0.0" on healthy squadmates
	// is noise, and noise on a medical panel is how the one line that matters gets missed.
	//------------------------------------------------------------------------------------------------
	protected static string BuildVitalsLine(ACM_TDFriendly friendly)
	{
		ACM_VitalsSnapshot v = friendly.vitals;
		if (!v || !v.valid)
			return "      --";

		string mark = "";
		if (v.derived)
			mark = "~";

		string line = "      ";

		if (v.hasHeartRate)
		{
			line += "HR ";
			line += mark;
			line += v.heartRate.ToString();
			line += "   ";
		}

		if (v.hasRespiration)
		{
			line += "BPM ";
			line += mark;
			line += v.breathsPerMinute.ToString();
		}

		if (v.hasBleeding && v.bleedRate > 0)
		{
			line += "   BLEED ";
			line += v.bleedRate.ToString(-1, 1);
		}

		return line;
	}

	//------------------------------------------------------------------------------------------------
	// Green while healthy, amber wounded, red unconscious, grey dead. Read at a glance, in that order
	// of urgency, without having to parse the numbers.
	//------------------------------------------------------------------------------------------------
	protected static ref Color VitalsColor(ACM_TDFriendly friendly)
	{
		return VitalsColorFor(friendly.vitals);
	}

	//------------------------------------------------------------------------------------------------
	protected static ref Color VitalsColorFor(ACM_VitalsSnapshot v)
	{
		if (!v || !v.valid)
			return Color.FromRGBA(120, 140, 130, 200);

		// Death and unconsciousness first - they change what you do, not just how fast.
		if (v.state == ACM_EVitalState.DEAD)
			return Color.FromRGBA(130, 130, 130, 220);

		if (v.state == ACM_EVitalState.UNCONSCIOUS)
			return Color.FromRGBA(255, 70, 60, 255);

		// Then the triage grade, walking yellow to red so urgency reads without parsing the word.
		switch (v.severity)
		{
			case ACM_EInjurySeverity.FATAL:		return Color.FromRGBA(255, 40, 40, 255);
			case ACM_EInjurySeverity.SEVERE:	return Color.FromRGBA(255, 120, 30, 255);
			case ACM_EInjurySeverity.MODERATE:	return Color.FromRGBA(255, 200, 50, 255);
			case ACM_EInjurySeverity.MINOR:		return Color.FromRGBA(220, 230, 110, 235);
		}

		return Color.FromRGBA(120, 200, 150, 200);
	}

	//------------------------------------------------------------------------------------------------
	protected static string BuildSquadLine(ACM_TDFriendly friendly, vector viewer)
	{
		string name = "AI";

		PlayerManager players = GetGame().GetPlayerManager();
		if (players && friendly.playerId > 0)
		{
			string resolved = ACM_PlayerNames.GetDisplayName(friendly.playerId);
			if (!resolved.IsEmpty())
			{
				resolved.ToUpper();

				// CLAMPED, because the row is a fixed-width plate and the fields to the right of the name
				// are the ones a medic actually reads. An overlong callsign must lose its own tail rather
				// than push the range, bearing and casualty state off the edge of the panel.
				if (resolved.Length() > NAME_MAX_CHARS)
					resolved = resolved.Substring(0, NAME_MAX_CHARS);

				name = resolved;
			}
		}

		string abbreviation;
		ACM_TacticalRolePreset preset = ACM_TacticalRoleCatalog.GetPreset(friendly.role);
		if (preset)
			abbreviation = preset.m_sAbbreviation;

		vector delta = friendly.entity.GetOrigin() - viewer;

		// Casualty state rides on the right of the identity line. It is the field a medic scans for,
		// so it goes where the eye lands last and stays there rather than moving with the text.
		string casualty = "";
		if (friendly.vitals && friendly.vitals.valid && friendly.vitals.state != ACM_EVitalState.HEALTHY)
		{
			casualty = "  ";
			casualty += friendly.vitals.StateLabel();
		}

		return string.Format("%1  %2  %3  %4m  %5%6",
			name,
			abbreviation,
			StatusText(friendly.status),
			Math.Round(friendly.distance),
			Pad3(HeadingOf(delta)),
			casualty);
	}

	//------------------------------------------------------------------------------------------------
	// What a squadmate is doing, in the few characters a strip row can spare.
	//
	// ENGAGING is deliberately not shortened to anything that could be misread as a claim about a
	// firefight. It means their weapon is up, which is exactly what the controller reports and
	// exactly what the wearer would see if they looked over.
	//
	// HOLDING prints nothing at all. A squad standing still is the resting state and does not need a
	// word each - and a column of identical text is a column the eye stops reading, which costs the
	// statuses that DO matter the attention they need.
	//------------------------------------------------------------------------------------------------
	protected static string StatusText(ACM_ETacticalStatus status)
	{
		if (status == ACM_ETacticalStatus.KIA)
			return "KIA";

		if (status == ACM_ETacticalStatus.DOWN)
			return "DOWN";

		if (status == ACM_ETacticalStatus.ENGAGING)
			return "ENGAGING";

		if (status == ACM_ETacticalStatus.MOUNTED)
			return "MOUNTED";

		if (status == ACM_ETacticalStatus.SPRINTING)
			return "SPRINT";

		if (status == ACM_ETacticalStatus.MOVING)
			return "MOVING";

		if (status == ACM_ETacticalStatus.UNKNOWN)
			return "--";

		return "";
	}

	//------------------------------------------------------------------------------------------------
	// KIA reads as grey rather than as a brighter red than DOWN.
	//
	// The distinction is the point: a casualty needs a medic and is the most urgent thing on the
	// strip, while somebody already dead is information rather than a task. Making them the loudest
	// row on the display would pull attention away from the squadmate who can still be saved.
	//------------------------------------------------------------------------------------------------
	protected static Color CasualtyColor(ACM_ETacticalStatus status)
	{
		if (status == ACM_ETacticalStatus.KIA)
			return Color.FromRGBA(150, 150, 155, 255);

		return Color.FromRGBA(245, 105, 85, 255);
	}

	//------------------------------------------------------------------------------------------------
	// TACTICAL EVENT LOG
	//
	// Row 0 is the newest and sits lowest. Everything here is a note about something that has
	// already happened somewhere else - the log holds no state any other system reads back, so the
	// whole panel could be deleted without changing what the device knows by a single contact.
	//------------------------------------------------------------------------------------------------
	protected static void RefreshLog()
	{
		float now = System.GetTickCount() / 1000.0;

		ACM_IvasEventLog.Prune(now);

		array<ref ACM_IvasEvent> events = ACM_IvasEventLog.GetEvents();

		for (int i = 0; i < s_aLogRows.Count(); i++)
		{
			TextWidget row = s_aLogRows[i];
			if (!row)
				continue;

			if (i >= events.Count())
			{
				row.SetVisible(false);
				continue;
			}

			ACM_IvasEvent entry = events[i];
			if (!entry)
			{
				row.SetVisible(false);
				continue;
			}

			row.SetText(entry.text);

			Color tint = ACM_IvasEventLog.ColorFor(entry.kind);
			float visibility = ACM_IvasEventLog.Visibility(entry, now);

			// Math.Round returns a FLOAT in this engine, not an int, so the channels are narrowed
			// deliberately here rather than left to an implicit conversion inside the call.
			int red = Math.Round(tint.R() * 255);
			int green = Math.Round(tint.G() * 255);
			int blue = Math.Round(tint.B() * 255);
			int alpha = Math.Round(255 * visibility);

			row.SetColor(Color.FromRGBA(red, green, blue, alpha));

			row.SetVisible(true);
		}
	}

	//------------------------------------------------------------------------------------------------
	// WEAPON READOUT
	//
	// Giving back what the suppressed vanilla weapon bar provided, in the visor's own styling.
	// Straight off the weapon: the selected weapon, its current fire mode name and the magazine's
	// own ammo count. Nothing derived, nothing estimated.
	//
	// The name goes on its own widget because UIInfo.GetName() hands back a "#AR-..." localization
	// key for vanilla weapons, and that widget is the one place in this layout with localization
	// left enabled. Concatenating the key into a line with the ammo count would print the key.
	//------------------------------------------------------------------------------------------------
	protected static void RefreshWeapon()
	{
		if (!s_wWeaponName && !s_wWeaponAmmo)
			return;

		BaseWeaponComponent weapon = ResolveCurrentWeapon();

		if (!weapon)
		{
			if (s_wWeaponName)
				s_wWeaponName.SetText("");

			if (s_wWeaponAmmo)
				s_wWeaponAmmo.SetText("NO WEAPON");

			return;
		}

		if (s_wWeaponName)
		{
			string name;
			UIInfo info = weapon.GetUIInfo();
			if (info)
				name = info.GetName();

			s_wWeaponName.SetText(name);
		}

		if (!s_wWeaponAmmo)
			return;

		string mode = weapon.GetCurrentFireModeName();
		mode.ToUpper();

		string ammo = "--/--";
		BaseMagazineComponent magazine = weapon.GetCurrentMagazine();
		if (magazine)
			ammo = string.Format("%1/%2", magazine.GetAmmoCount(), magazine.GetMaxAmmoCount());

		if (mode.IsEmpty())
			s_wWeaponAmmo.SetText(ammo);
		else
			s_wWeaponAmmo.SetText(mode + "   " + ammo);
	}

	//------------------------------------------------------------------------------------------------
	protected static BaseWeaponComponent ResolveCurrentWeapon()
	{
		IEntity local = SCR_PlayerController.GetLocalControlledEntity();
		if (!local)
			return null;

		SCR_CharacterControllerComponent controller = SCR_CharacterControllerComponent.Cast(local.FindComponent(SCR_CharacterControllerComponent));
		if (!controller)
			return null;

		BaseWeaponManagerComponent manager = controller.GetWeaponManagerComponent();
		if (!manager)
			return null;

		return manager.GetCurrentWeapon();
	}

	//------------------------------------------------------------------------------------------------
	// GRID REFERENCE
	//
	// The game's own grid, not one worked out here. SCR_MapEntity owns the terrain's grid
	// configuration, so asking it is the only way this readout is guaranteed to match the number on
	// the map the wearer is calling it in on - a hand-rolled kilometre grid would agree on some
	// terrains and quietly disagree on others, which is the worst possible failure for a readout
	// people call fire missions off.
	//
	// Guarded on the map entity existing. A world with no map entity returns no grid rather than
	// dereferencing one, and the readout goes blank instead of the client going down.
	//------------------------------------------------------------------------------------------------
	protected static void RefreshGrid()
	{
		if (!s_wGrid)
			return;

		vector origin = ViewerOrigin();
		if (origin == vector.Zero)
		{
			s_wGrid.SetText("");
			return;
		}

		if (!SCR_MapEntity.GetMapInstance())
		{
			s_wGrid.SetText(string.Format("ALT %1m", Math.Round(origin[1])));
			return;
		}

		int gridX, gridZ;
		SCR_MapEntity.GetGridPos(origin, gridX, gridZ);

		s_wGrid.SetText(string.Format("GRID %1 %2   ALT %3m", gridX, gridZ, Math.Round(origin[1])));
	}

	//------------------------------------------------------------------------------------------------
	// Shared helpers
	//------------------------------------------------------------------------------------------------

	//------------------------------------------------------------------------------------------------
	// Compass heading of a direction, 0..360 with north at zero.
	//
	// Atan2(x, z) rather than the usual (y, x): Enfusion's world plane is X east and Z north, so
	// this is the pair that puts zero at north and increases clockwise, which is what a compass
	// means by a bearing.
	//------------------------------------------------------------------------------------------------
	protected static float HeadingOf(vector direction)
	{
		float degrees = Math.Atan2(direction[0], direction[2]) * Math.RAD2DEG;

		while (degrees < 0)
			degrees += 360.0;

		while (degrees >= 360.0)
			degrees -= 360.0;

		return degrees;
	}

	//------------------------------------------------------------------------------------------------
	// Signed shortest way round, -180..180. What makes the ribbon wrap rather than run off its end.
	//------------------------------------------------------------------------------------------------
	protected static float NormalizeDelta(float degrees)
	{
		while (degrees > 180.0)
			degrees -= 360.0;

		while (degrees <= -180.0)
			degrees += 360.0;

		return degrees;
	}

	//------------------------------------------------------------------------------------------------
	protected static string CardinalOf(float heading)
	{
		int sector = Math.Round(heading / 45.0);


		while (sector < 0)
			sector += 8;

		while (sector > 7)
			sector -= 8;

		if (sector == 0)
			return "N";

		if (sector == 1)
			return "NE";

		if (sector == 2)
			return "E";

		if (sector == 3)
			return "SE";

		if (sector == 4)
			return "S";

		if (sector == 5)
			return "SW";

		if (sector == 6)
			return "W";

		return "NW";
	}

	//------------------------------------------------------------------------------------------------
	// Three-figure bearing, the way a bearing is actually said out loud. 047, not 47.
	//------------------------------------------------------------------------------------------------
	protected static string Pad3(float degrees)
	{
		int value = Math.Round(degrees);

		if (value < 0)
			value = 0;

		if (value > 359)
			value = 359;

		if (value < 10)
			return "00" + value.ToString();

		if (value < 100)
			return "0" + value.ToString();

		return value.ToString();
	}

	//------------------------------------------------------------------------------------------------
	protected static vector ViewerOrigin()
	{
		IEntity local = SCR_PlayerController.GetLocalControlledEntity();
		if (!local)
			return vector.Zero;

		return local.GetOrigin();
	}

	//------------------------------------------------------------------------------------------------
	protected static string ResolveCallsign()
	{
		PlayerController controller = GetGame().GetPlayerController();
		PlayerManager players = GetGame().GetPlayerManager();

		if (!controller || !players)
			return "---";

		string name = ACM_PlayerNames.GetDisplayName(controller.GetPlayerId());
		if (name.IsEmpty())
			return "---";

		name.ToUpper();
		return name;
	}

	//------------------------------------------------------------------------------------------------
	// The EFFECTIVE role, so a player who has declared nothing still reads as something rather than
	// as blank. Same resolver the friendly IFF markers use, so the visor and the markers can never
	// disagree about what somebody is.
	//------------------------------------------------------------------------------------------------
	protected static string ResolveRole()
	{
		PlayerController controller = GetGame().GetPlayerController();
		IEntity local = SCR_PlayerController.GetLocalControlledEntity();

		if (!controller || !local)
			return "";

		ACM_ETacticalRole role = ACM_TacticalRoleComponent.ResolveEffectiveRole(controller.GetPlayerId(), local);

		ACM_TacticalRolePreset preset = ACM_TacticalRoleCatalog.GetPreset(role);
		if (!preset)
			return "";

		string label = preset.m_sDisplayName;
		label.ToUpper();
		return label;
	}

	//------------------------------------------------------------------------------------------------
	// Thermal wins the readout when both are lit, because thermal is what the wearer is actually
	// looking through at that point.
	//------------------------------------------------------------------------------------------------
	protected static string ResolveMode()
	{
		if (!s_Device)
			return "DAY";

		ACM_ThermalComponentV2 thermal = ACM_ThermalComponentV2.Cast(s_Device.FindComponent(ACM_ThermalComponentV2));
		if (thermal && thermal.IsThermalActive())
			return "THERMAL / " + thermal.GetCurrentPaletteName();

		ACM_NightVisionComponent nvg = ACM_NightVisionComponent.Cast(s_Device.FindComponent(ACM_NightVisionComponent));
		if (nvg && nvg.IsNVGActive())
			return "NV / " + nvg.GetCurrentColorName();

		return "DAY";
	}

	//------------------------------------------------------------------------------------------------
	// Reported from the contact STORE rather than from what is currently drawn, so a display budget
	// squeezing markers off the screen can never change this number. It says what the device knows.
	//------------------------------------------------------------------------------------------------
	protected static string ResolveStatus()
	{
		ACM_TargetDetectionSystem system = ACM_TargetDetectionSystem.GetInstance();
		if (!system)
			return "";

		int total = system.GetContactCount();
		if (total == 0)
			return "NO CONTACTS";

		return string.Format("%1 CONTACTS / %2 LINK", total, system.GetLinkContactCount());
	}

	//------------------------------------------------------------------------------------------------
	protected static bool CreateWidgets()
	{
		if (s_wRoot)
			return true;

		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if (!workspace)
			return false;

		s_wRoot = workspace.CreateWidgets(VISOR_LAYOUT);
		if (!s_wRoot)
		{
			Print("[ACM IVAS] Visor layout failed to load. Open the addon in Workbench once so UI/layouts/HUD/ACM_IvasVisor.layout is registered.", LogLevel.WARNING);
			return false;
		}

		s_wId = TextWidget.Cast(s_wRoot.FindAnyWidget("ACM_VisorId"));
		s_wRole = TextWidget.Cast(s_wRoot.FindAnyWidget("ACM_VisorRole"));
		s_wMode = TextWidget.Cast(s_wRoot.FindAnyWidget("ACM_VisorMode"));
		s_wStatus = TextWidget.Cast(s_wRoot.FindAnyWidget("ACM_VisorStatus"));

		s_wRibbonCanvas = CanvasWidget.Cast(s_wRoot.FindAnyWidget("ACM_VisorRibbonCanvas"));
		s_wHeading = TextWidget.Cast(s_wRoot.FindAnyWidget("ACM_VisorHeading"));

		s_aRibbonLabels.Clear();
		for (int i = 0; i < RIBBON_LABELS; i++)
			s_aRibbonLabels.Insert(TextWidget.Cast(s_wRoot.FindAnyWidget("ACM_VisorRibbonLabel" + i.ToString())));

		s_wSquadHeader = TextWidget.Cast(s_wRoot.FindAnyWidget("ACM_VisorSquadHeader"));
		s_wSquadPanel = s_wRoot.FindAnyWidget("ACM_VisorPanelSquad");

		s_aSquadRows.Clear();
		for (int s = 0; s < SQUAD_ROWS; s++)
			s_aSquadRows.Insert(TextWidget.Cast(s_wRoot.FindAnyWidget("ACM_VisorSquad" + s.ToString())));

		s_wCasualtyPanel = s_wRoot.FindAnyWidget("ACM_VisorCasualtyPanel");
		s_wCasualtyName = TextWidget.Cast(s_wRoot.FindAnyWidget("ACM_VisorCasualtyName"));
		s_wCasualtyState = TextWidget.Cast(s_wRoot.FindAnyWidget("ACM_VisorCasualtyState"));

		s_aCasualtyRows.Clear();
		for (int c = 0; c < CASUALTY_ROWS; c++)
			s_aCasualtyRows.Insert(TextWidget.Cast(s_wRoot.FindAnyWidget("ACM_VisorCasualty" + c.ToString())));

		s_aLogRows.Clear();
		for (int e = 0; e < ACM_IvasEventLog.MAX_EVENTS; e++)
			s_aLogRows.Insert(TextWidget.Cast(s_wRoot.FindAnyWidget("ACM_VisorLog" + e.ToString())));

		s_wWeaponName = TextWidget.Cast(s_wRoot.FindAnyWidget("ACM_VisorWeaponName"));
		s_wWeaponAmmo = TextWidget.Cast(s_wRoot.FindAnyWidget("ACM_VisorWeaponAmmo"));
		s_wGrid = TextWidget.Cast(s_wRoot.FindAnyWidget("ACM_VisorGrid"));

		s_iScaleColor = Color.FromRGBA(110, 180, 145, 130).PackToInt();
		s_iCaretColor = Color.FromRGBA(190, 245, 215, 235).PackToInt();

		return true;
	}
}
