//------------------------------------------------------------------------------------------------
// ACM_IvasBootSequence.c
// The power-on splash: a POST-style checklist that runs once when an IVAS comes up.
//
// PURELY LOCAL, AND THAT IS THE WHOLE DESIGN. This draws on one person's screen and nothing else.
// No signal, no RPC, no replicated state, no authority question - the wearer's client decides to
// play it and nobody else ever hears about it. After everything the visor itself needed to work
// across a server, it is worth being explicit that this needed none of it.
//
// Static rather than an instance, and keyed per device, because a player can carry two IVAS units
// and each earns its own boot. The state array is also what makes "once per activation" true: a
// device already in the list is refused, so holding the toggle or a double-fired input cannot
// restart the sequence half way through.
//
// STRUCTURED SO A FLIPBOOK CAN REPLACE THE TEXT LATER. The ACM_V2 ATAK splash animates a 124-frame
// image sequence by setting WidgetFlags.CUSTOMUV on an ImageWidget and walking SetUV() across a
// grid atlas, driven by exactly the progress value this class already computes. If that art ever
// exists, it drops into ApplyFrame beside the text; nothing else here changes.
//------------------------------------------------------------------------------------------------
class ACM_IvasBootState
{
	int m_iStartMs;
	int m_iDurationMs;
	IEntity m_pDevice;
}

class ACM_IvasBootSequence
{
	// Fully qualified now that Workbench has indexed the .meta and issued an id.
	//
	// A path-only reference resolves, but a registered id is what the resource database actually
	// wants - and this project has already been round the loop of a layout with no .meta logging
	// "Wrong GUID for resource {0000000000000000}" on every load.
	protected static const ResourceName BOOT_LAYOUT = "{95CBE182CF6C7C7B}UI/layouts/HUD/ACM_IvasBoot.layout";

	// THE ONE NUMBER TO TUNE. Total run length in milliseconds, picked at random per boot.
	//
	// Everything else scales off it: the checklist reveal is derived from progress, so a longer run
	// spaces the lines out rather than leaving them bunched at the start with dead time after. Only
	// the logo and tagline fades are absolute, and they are meant to be - the mark should come up
	// briskly however long the rest takes.
	//
	// Varied so two boots never feel like the same canned clip. The difference is noticeable by the
	// third or fourth time somebody switches on.
	protected static const ref array<int> DURATION_POOL_MS = {5200, 5800, 6400, 7000};

	protected static const int TICK_MS = 16;
	protected static const int FADE_OUT_MS = 450;
	protected static const int TAGLINE_FADE_MS = 500;

	// The logo leads. Manufacturer mark first, checklist under it - which is the order real hardware
	// boots in, and it gives the sequence somewhere to start before any text appears.
	protected static const int LOGO_FADE_MS = 400;

	// Must match the number of ACM_BootLine* widgets in the layout.
	protected static const int LINE_COUNT = 10;

	// Must match SizeX on ACM_BootProgressFill. The fill is animated by pulling its RIGHT offset
	// from the full width back to zero, which is the same trick the ATAK splash uses - a widget
	// cannot be resized from a slot without touching offsets.
	protected static const float BAR_WIDTH = 520.0;

	// The checklist. Reveal order is top to bottom and the timing is derived, so adding a line here
	// is the only edit needed to lengthen the sequence - as long as the layout has a widget for it.
	protected static const ref array<string> LINES = {
		"  CORE ................................. OK",
		"  POWER CELL ........................... OK",
		"  OPTICS BUS ........................... OK",
		"  PHOSPHOR DRIVER ...................... OK",
		"  THERMAL ARRAY ........................ OK",
		"  IFF TRANSPONDER ...................... OK",
		"  TACTICAL DATA LINK ................... OK",
		"  REMOTE FEED RECEIVER ................. OK",
		"  CONTACT PROCESSOR .................... OK",
		"  ALL SYSTEMS NOMINAL"
	};

	// Devices that have ALREADY COMPLETED a boot, and will never play another.
	//
	// A real device runs its power-on self test once, when it is first brought up - not every time
	// somebody drops the visor. So this is per DEVICE ENTITY and permanent: pick up a different IVAS
	// and that one boots, because it is a different entity with no entry here.
	//
	// Recorded on COMPLETION, not on start. Flipping the visor down and straight back up aborts the
	// sequence a second in, and burning the device's one and only boot on a run nobody watched would
	// be the wrong trade - that device gets another chance.
	//
	// Weak references. Entities are nulled here when they are destroyed, and Forget() prunes on
	// delete so the list cannot grow without bound over a long session.
	protected static ref array<IEntity> s_aBooted = new array<IEntity>();

	protected static ref array<ref ACM_IvasBootState> s_aStates = new array<ref ACM_IvasBootState>();
	protected static Widget s_wRoot;
	protected static ref array<TextWidget> s_aLines = new array<TextWidget>();

	//------------------------------------------------------------------------------------------------
	// Called when a device powers on for the person wearing it.
	//------------------------------------------------------------------------------------------------
	static void Play(IEntity device)
	{
		if (!device)
			return;

		// A dedicated server has no workspace to draw on. Same guard the feed renderer opens with.
		if (!GetGame() || !GetGame().GetWorkspace())
			return;

		// This device has booted before. That is the end of it.
		if (HasBooted(device))
			return;

		// Already running for this device - a double-fired input, or the visor toggled twice inside
		// the sequence. Refusing here stops it restarting halfway through.
		foreach (ACM_IvasBootState existing : s_aStates)
		{
			if (existing && existing.m_pDevice == device)
				return;
		}

		if (!CreateWidgets())
			return;

		ACM_IvasBootState state = new ACM_IvasBootState();
		state.m_iStartMs = GetGame().GetWorld().GetWorldTime();
		state.m_iDurationMs = DURATION_POOL_MS[Math.RandomInt(0, DURATION_POOL_MS.Count())];
		state.m_pDevice = device;
		s_aStates.Insert(state);

		GetGame().GetCallqueue().Remove(Tick);
		GetGame().GetCallqueue().CallLater(Tick, TICK_MS, true);
	}

	//------------------------------------------------------------------------------------------------
	// Stop immediately and clean up - the device was switched off, dropped, or the wearer died
	// part-way through. Leaving a splash on screen after the thing playing it is gone would be the
	// worst possible version of this feature.
	//------------------------------------------------------------------------------------------------
	static void Abort(IEntity device)
	{
		for (int i = s_aStates.Count() - 1; i >= 0; i--)
		{
			if (s_aStates[i] && s_aStates[i].m_pDevice == device)
				s_aStates.Remove(i);
		}

		if (s_aStates.IsEmpty())
			Finish();
	}

	//------------------------------------------------------------------------------------------------
	// Has this device already run its one boot? Prunes dead entries while it looks.
	//------------------------------------------------------------------------------------------------
	static bool HasBooted(IEntity device)
	{
		if (!device)
			return true;

		bool found = false;

		for (int i = s_aBooted.Count() - 1; i >= 0; i--)
		{
			if (!s_aBooted[i])
			{
				s_aBooted.Remove(i);
				continue;
			}

			if (s_aBooted[i] == device)
				found = true;
		}

		return found;
	}

	//------------------------------------------------------------------------------------------------
	// The device is gone - destroyed, streamed out, or the wearer lost it. Drop its entry so the
	// list tracks live hardware rather than accumulating every IVAS the session has ever seen.
	//------------------------------------------------------------------------------------------------
	static void Forget(IEntity device)
	{
		Abort(device);

		int index = s_aBooted.Find(device);
		if (index != -1)
			s_aBooted.Remove(index);
	}

	//------------------------------------------------------------------------------------------------
	static bool IsPlaying()
	{
		return !s_aStates.IsEmpty();
	}

	//------------------------------------------------------------------------------------------------
	protected static void Tick()
	{
		// A menu opening is a teardown case, not a skip case, exactly as it is for the contact
		// overlay: leaving this on screen underneath an open inventory reads as a stuck HUD.
		if (GetGame().GetMenuManager() && GetGame().GetMenuManager().IsAnyMenuOpen())
		{
			s_aStates.Clear();
			Finish();
			return;
		}

		int now = GetGame().GetWorld().GetWorldTime();

		// Backwards, so removing during iteration is safe.
		for (int i = s_aStates.Count() - 1; i >= 0; i--)
		{
			ACM_IvasBootState state = s_aStates[i];

			if (!state || !state.m_pDevice)
			{
				s_aStates.Remove(i);
				continue;
			}

			int elapsed = now - state.m_iStartMs;
			if (elapsed >= state.m_iDurationMs)
			{
				// Ran to the end, so this device is done for good. Abort deliberately does not do
				// this, which is what gives a cancelled boot another chance.
				if (s_aBooted.Find(state.m_pDevice) == -1)
					s_aBooted.Insert(state.m_pDevice);

				s_aStates.Remove(i);
				continue;
			}

			ApplyFrame(elapsed, state.m_iDurationMs);
		}

		if (s_aStates.IsEmpty())
			Finish();
	}

	//------------------------------------------------------------------------------------------------
	// One frame of the sequence. Everything is derived from elapsed time rather than accumulated, so
	// a dropped frame or a hitch cannot desynchronise the reveal from the progress bar.
	//------------------------------------------------------------------------------------------------
	protected static void ApplyFrame(int elapsed, int durationMs)
	{
		if (!s_wRoot)
			return;

		float progress = Progress(elapsed, durationMs);

		s_wRoot.SetOpacity(Opacity(elapsed, durationMs));

		// Lines appear across the portion of the run before the fade begins, so the last one lands
		// while the splash is still fully solid rather than during the fade.
		int revealed = Math.Clamp(Math.Ceil(progress * LINE_COUNT), 0, LINE_COUNT);

		for (int i = 0; i < s_aLines.Count(); i++)
		{
			TextWidget line = s_aLines[i];
			if (!line)
				continue;

			line.SetVisible(i < revealed);
		}

		Widget fill = s_wRoot.FindAnyWidget("ACM_BootProgressFill");
		if (fill)
			FrameSlot.SetOffsets(fill, 0, 0, BAR_WIDTH * (1.0 - progress), 0);

		Widget logo = s_wRoot.FindAnyWidget("ACM_BootLogo");
		if (logo)
		{
			float logoAlpha = 1.0;
			if (elapsed < LOGO_FADE_MS)
				logoAlpha = elapsed * 1.0 / LOGO_FADE_MS;

			logo.SetOpacity(logoAlpha);
		}

		Widget tagline = s_wRoot.FindAnyWidget("ACM_BootTagline");
		if (tagline)
		{
			float taglineAlpha = 1.0;
			if (elapsed < TAGLINE_FADE_MS)
				taglineAlpha = elapsed * 1.0 / TAGLINE_FADE_MS;

			tagline.SetOpacity(taglineAlpha);
		}
	}

	//------------------------------------------------------------------------------------------------
	// Solid until the tail, then out. No fade in: the display is coming on, and a screen that eases
	// itself up reads as slow rather than as booting.
	//------------------------------------------------------------------------------------------------
	protected static float Opacity(int elapsed, int durationMs)
	{
		int fadeStart = durationMs - FADE_OUT_MS;
		if (fadeStart <= 0 || elapsed <= fadeStart)
			return 1.0;

		return (durationMs - elapsed) * 1.0 / FADE_OUT_MS;
	}

	//------------------------------------------------------------------------------------------------
	// Reaches 1.0 when the fade begins rather than at the very end, so the bar fills and the last
	// line lands before anything starts disappearing.
	//------------------------------------------------------------------------------------------------
	protected static float Progress(int elapsed, int durationMs)
	{
		int progressEnd = durationMs - FADE_OUT_MS;
		if (progressEnd <= 0 || elapsed >= progressEnd)
			return 1.0;

		return elapsed * 1.0 / progressEnd;
	}

	//------------------------------------------------------------------------------------------------
	// Its OWN root, never parented to the contact overlay.
	//
	// That root is created and destroyed on ordinary events - a menu opening, the goggles switching
	// off, a respawn - and anything hanging beneath it dies with it. A splash parented there would be
	// killed mid-play by a teardown that has nothing to do with it. Same reason the remote feed keeps
	// its own hierarchy.
	//------------------------------------------------------------------------------------------------
	protected static bool CreateWidgets()
	{
		if (s_wRoot)
			return true;

		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if (!workspace)
			return false;

		s_wRoot = workspace.CreateWidgets(BOOT_LAYOUT);
		if (!s_wRoot)
		{
			Print("[ACM IVAS] Boot splash layout failed to load. Open the addon in Workbench once so UI/layouts/HUD/ACM_IvasBoot.layout is registered.", LogLevel.WARNING);
			return false;
		}

		s_aLines.Clear();
		for (int i = 0; i < LINE_COUNT; i++)
		{
			TextWidget line = TextWidget.Cast(s_wRoot.FindAnyWidget("ACM_BootLine" + i.ToString()));

			if (line && i < LINES.Count())
				line.SetText(LINES[i]);

			if (line)
				line.SetVisible(false);

			s_aLines.Insert(line);
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected static void Finish()
	{
		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().Remove(Tick);

		s_aLines.Clear();

		if (s_wRoot)
		{
			s_wRoot.RemoveFromHierarchy();
			s_wRoot = null;
		}
	}
}
