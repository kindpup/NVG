//------------------------------------------------------------------------------------------------
// ACM_RemoteFeedRenderer.c
// Viewer half of the Remote Feed. One camera slot, one render target, one panel.
//
// Enfusion does not transmit video. A render target is client-side and none of the image crosses
// the network - this client points a spare world camera at the source's lens each frame and
// renders it locally. Only which source, and whether it is powered, ever need to replicate.
//
// No camera entity. The engine has a small pool of world camera slots addressed by index, driven
// with SetCameraEx. An entity to represent one just adds a lifetime and an event mask to get wrong.
//
// Separate from the target detection overlay - own layout, own lifetime. Hiding a PiP has to stop
// the render target, not just hide a widget.
//------------------------------------------------------------------------------------------------

class ACM_RemoteFeedRenderer
{
	protected static const ResourceName FEED_LAYOUT = "{67200001EEEE0001}UI/layouts/HUD/ACM_RemoteFeed.layout";

	// Few slots, shared with every other PiP system in the game. 0 is the main player camera.
	protected static const int CAM_SLOT_MIN = 11;
	protected static const int CAM_SLOT_MAX = 15;

	// Parks the render target without releasing it - the widget keeps its last contents. There is
	// no pause on RenderTargetWidget, only a frame divider.
	protected static const int PARKED_REFRESH_PERIOD = 100000;
	protected static const int NORMAL_REFRESH_PERIOD = 1;

	protected static const int ARBITRATION_INTERVAL_MS = 100;

	// Interference for the feed camera. ACM grain assets, not the ACM_V2 copies.
	//
	// Priorities 10 and 12 because ACM_NightVisionComponent mirrors its chain onto this same camera
	// using 8, 9, 11 and 16. Sharing a priority would have the two overwrite each other.
	protected static const ResourceName SIGNAL_GRAIN = "{3D9639C3DBCCCD87}Assets/Shaders/NVSystem/Grains/ACM_WhiteGrain.emat";
	protected static const ResourceName SIGNAL_BLEMISH = "{6C0F65808E9A88C5}Assets/Shaders/NVSystem/Grains/ACM_SignalBlemish.emat";
	protected static const int PP_PRIORITY_SIGNAL = 10;
	protected static const int PP_PRIORITY_SIGNAL_HEAVY = 12;

	// The render target has linear filtering off, so dropping resolution gives hard pixels rather
	// than a blur. This is how far down it goes.
	// The asset is authored at 0.05, tuned as a gentle NVG grain - applied as-is it is invisible.
	// Signal interference needs an order of magnitude more.
	protected static const float GRAIN_INTENSITY_MIN = 0.04;
	protected static const float GRAIN_INTENSITY_MAX = 0.85;
	protected static const float GRAIN_SIZE_MIN = 1.0;
	protected static const float GRAIN_SIZE_MAX = 5.0;

	protected static const float MIN_RESOLUTION_SCALE = 0.12;
	protected static const float RESOLUTION_CURVE_POWER = 1.6;

	// The same press can arrive twice - two of the same device carried at once, each with its own
	// listener. Two toggles cancel out and look like the feed refusing to start.
	protected static const float TOGGLE_DEBOUNCE_MS = 250;

	// Off for production. Feed chatter is per-source and per-band, which is exactly the sort of
	// thing that buries a real warning during a live session.
	protected static const bool ACM_FEED_DEBUG = false;

	protected static ref ACM_RemoteFeedRenderer s_Instance;

	//------------------------------------------------------------------------------------------------
	// Configuration
	//------------------------------------------------------------------------------------------------
	protected int m_iCamSlot = CAM_SLOT_MIN;

	// Panel position and size are NOT set here. They belong to ACM_RemoteFeed.layout, where they
	// can be edited in the Workbench UI editor and actually seen. The renderer only reads the
	// panel's size, to size the render target to match.
	//
	// The one exception is display-region mode below, where a device asks for the feed to be
	// placed inside its own optic mask - that necessarily overrides whatever the layout said.
	protected float m_fRenderScale = 0.5;
	protected int m_iMaxFPS = 30;

	protected ACM_ERemoteFeedArbitration m_eArbitration = ACM_ERemoteFeedArbitration.COEXIST;

	// The wearer's display region, pushed in by the device. Null means no device has declared one
	// and the feed falls back to a fixed size in a screen corner.
	protected ref ACM_RemoteFeedDisplayRegion m_Region;
	protected float m_fRegionSizeFraction = 0.42;
	protected float m_fRegionMargin = 0.04;

	// Player-chosen placement, on top of whatever the device authored.
	//
	// The size preset is a MULTIPLIER rather than an absolute, so a device that authored a small
	// feed still gets a small feed at LARGE - the preset moves it relative to what the prefab asked
	// for instead of overwriting it.
	protected float m_fSizeScale = 1.0;
	protected ACM_ERemoteFeedCorner m_eCorner = ACM_ERemoteFeedCorner.TOP_LEFT;

	// Screen-docked placement, used when no device declared a display region. Width as a fraction
	// of the screen, and the inset from whichever corner was chosen.
	protected float m_fScreenSizeFraction = 0.26;
	protected float m_fScreenMargin = 0.03;

	// A compatibility addon asking the feed to stand down while something else owns the screen.
	//
	// Deliberately a bare flag and a reason string. Core has no idea what is suppressing it and
	// must not: the moment this file knows the name of another mod's camera HUD, the boundary is
	// gone. Anything that wants to drive this lives outside ACM IVAS core.
	protected static bool s_bExternallySuppressed;
	protected static string s_sSuppressionReason;
	protected bool m_bWantedBeforeSuppression;

	//------------------------------------------------------------------------------------------------
	// Runtime
	//------------------------------------------------------------------------------------------------
	protected Widget m_wRoot;
	protected Widget m_wPanel;
	protected RenderTargetWidget m_wRenderTarget;
	protected CanvasWidget m_wCanvas;
	protected TextWidget m_wTitle;
	protected TextWidget m_wStatus;
	protected TextWidget m_wOwner;
	protected TextWidget m_wLink;
	protected int m_iShownOwnerId = -1;

	protected bool m_bActive;

	//------------------------------------------------------------------------------------------------
	// FADE
	//
	// Resting opacity is deliberately under 1. A fully opaque panel reads as a sticker on the
	// screen; a slightly translucent one reads as something being PROJECTED onto the visor, which is
	// what it is meant to be. Low enough to feel like glass, high enough that the picture is still
	// the picture - past about 0.8 the terrain behind starts competing with the feed.
	//
	// The two durations differ on purpose. Coming up wants to feel like a device waking, going down
	// wants to be out of the way quickly - a slow fade-out on something you just switched off feels
	// like lag rather than polish.
	protected static const float FEED_OPACITY = 0.90;
	protected static const int FADE_IN_MS = 260;
	protected static const int FADE_OUT_MS = 200;
	protected static const int FADE_TICK_MS = 16;

	// Where the fade currently is, 0..1, BEFORE the resting opacity is applied. Kept separate from
	// FEED_OPACITY so that retuning the resting value cannot change how the fade behaves.
	protected float m_fFadeProgress;
	protected bool m_bFading;
	protected bool m_bFadingOut;
	protected bool m_bVisorDown = true;
	protected bool m_bWantedBeforeVisor;
	protected bool m_bOpticActive;
	protected ACM_ERemoteFeedState m_eState = ACM_ERemoteFeedState.STOPPED;

	protected vector m_mFeedTransform[4];
	protected bool m_bHasTransform;
	protected float m_fFieldOfView = 70.0;
	protected float m_fExposureScale = 1.0;

	protected ACM_RemoteFeedSourceComponent m_Source;
	protected ref array<ACM_RemoteFeedSourceComponent> m_aSourceScratch = new array<ACM_RemoteFeedSourceComponent>();

	protected bool m_bTargetBound;
	protected int m_iMirroredNvgVersion = -1;
	protected bool m_bWarnedZeroSize;
	protected float m_fLastToggleTime;

	// Owns an evaluator but does none of its work - reads a number, shows the consequences.
	protected ref ACM_RemoteSignalEvaluator m_Signal = new ACM_RemoteSignalEvaluator();
	protected ref array<vector> m_aLinkPath = new array<vector>();
	protected int m_iAppliedBand = -1;

	// Forced quality for testing, -1 for off. Staging a weak link is awkward - a camera worn on your
	// own body sits 20cm from the viewer and pins at full strength.
	protected float m_fDebugQuality = -1;
	protected float m_fLastLinkLog;

	// Held so its parameters can be driven rather than reloaded every evaluation.
	protected ref Material m_SignalGrain;
	protected float m_fAppliedInterference = -1;
	protected float m_fAppliedResolution = -1;
	protected ACM_ERemoteFeedLinkState m_eAppliedLinkState = ACM_ERemoteFeedLinkState.NONE;
	protected float m_fLastCycleTime;

	// Test source only. Removed once every deployment has real sources authored.
	protected float m_fAimYaw;
	protected float m_fAimPitch;
	protected float m_fTestHeight = 8.0;

	//------------------------------------------------------------------------------------------------
	// Strongest first; first band whose floor the quality clears wins.
	//
	// A weaker link is cheaper, not just uglier - noise drawn over a full-rate render would make the
	// worst-looking feed the most expensive one.
	protected static ref array<ref ACM_RemoteFeedBand> s_aBands;

	//------------------------------------------------------------------------------------------------
	protected static array<ref ACM_RemoteFeedBand> Bands()
	{
		if (s_aBands)
			return s_aBands;

		s_aBands = new array<ref ACM_RemoteFeedBand>();
		s_aBands.Insert(new ACM_RemoteFeedBand(0.80, 30, 1.00, 0.00, "LINK GOOD"));
		s_aBands.Insert(new ACM_RemoteFeedBand(0.60, 24, 0.95, 0.15, ""));
		s_aBands.Insert(new ACM_RemoteFeedBand(0.40, 15, 0.80, 0.35, ""));
		s_aBands.Insert(new ACM_RemoteFeedBand(0.20, 10, 0.60, 0.65, "LINK POOR"));
		s_aBands.Insert(new ACM_RemoteFeedBand(0.05, 6, 0.45, 0.90, "SIGNAL WEAK"));

		return s_aBands;
	}

	//------------------------------------------------------------------------------------------------
	static ACM_RemoteFeedRenderer GetInstance()
	{
		if (!s_Instance)
			s_Instance = new ACM_RemoteFeedRenderer();

		return s_Instance;
	}

	//------------------------------------------------------------------------------------------------
	// Lifecycle
	//------------------------------------------------------------------------------------------------
	void Start()
	{
		// Caught BEFORE the m_bActive guard, and it has to be: the panel is still active while it
		// fades out, so without this the early return below would swallow the request and the feed
		// would carry on disappearing after the wearer asked for it back.
		if (m_bActive && m_bFading && m_bFadingOut)
		{
			m_bFadingOut = false;
			BeginFade(false);
			return;
		}

		if (m_bActive)
			return;

		// A dedicated server has no workspace and nothing to render into.
		if (!GetGame() || !GetGame().GetWorkspace())
			return;

		// Something else owns the screen. Remember that the wearer asked, so lifting the
		// suppression puts the feed back rather than leaving it dark.
		if (s_bExternallySuppressed)
		{
			m_bWantedBeforeSuppression = true;
			return;
		}

		m_bActive = true;

		if (!CreateWidgets())
		{
			m_bActive = false;
			return;
		}

		// From nothing, so the first frame the widget exists is already transparent. Setting this
		// after the callqueue entry would show one opaque frame before the fade took hold.
		m_fFadeProgress = 0;
		ApplyFadeOpacity();
		BeginFade(false);

		if (GetGame().GetCallqueue())
		{
			GetGame().GetCallqueue().Remove(ArbitrationTick);
			GetGame().GetCallqueue().Remove(DriveTick);
			GetGame().GetCallqueue().CallLater(ArbitrationTick, ARBITRATION_INTERVAL_MS, true);
			GetGame().GetCallqueue().CallLater(DriveTick, 0, true);
		}

		// Falls through to the test camera when nothing is authored anywhere.
		if (!m_Source)
			CycleSource();

		UpdateTitle();
		Evaluate();

		float panelW, panelH;
		if (m_wPanel)
			m_wPanel.GetScreenSize(panelW, panelH);

		if (ACM_FEED_DEBUG)
			Print("[ACM RemoteFeed] Started on camera slot " + m_iCamSlot.ToString() + ". Panel " + panelW.ToString() + "x" + panelH.ToString() + " from the layout, render scale " + m_fRenderScale.ToString() + ", max " + m_iMaxFPS.ToString() + " fps.", LogLevel.DEBUG);
	}

	//------------------------------------------------------------------------------------------------
	// Full release - slot no longer driven, render target unbound rather than hidden.
	//------------------------------------------------------------------------------------------------
	void Stop()
	{
		if (!m_bActive && m_eState == ACM_ERemoteFeedState.STOPPED)
			return;

		m_bActive = false;

		// Stop() is the HARD release and stays that way. A fade in progress is abandoned rather
		// than waited for, so the teardown paths that call this - death, unequip, master off,
		// suppression - release the camera slot the instant they ask to.
		CancelFade();

		if (GetGame() && GetGame().GetCallqueue())
		{
			GetGame().GetCallqueue().Remove(ArbitrationTick);
			GetGame().GetCallqueue().Remove(DriveTick);
		}

		ApplyInterference(0);
		ReleaseGrain();
		ACM_NightVisionComponent.ClearMirroredEffects(m_iCamSlot);
		m_iMirroredNvgVersion = -1;

		DetachRenderTarget();
		DestroyWidgets();

		m_eState = ACM_ERemoteFeedState.STOPPED;
		m_bOpticActive = false;
		m_bTargetBound = false;
		m_bWarnedZeroSize = false;

		if (ACM_FEED_DEBUG)
			Print("[ACM RemoteFeed] Stopped. Camera slot released, render target unbound.", LogLevel.DEBUG);
	}

	// The player-facing way off.
	//
	// Distinct from Stop(), which is the hard release every teardown path needs. This one lets the
	// panel fade before releasing anything, then calls Stop() to do the real work - so there is
	// still exactly ONE place that unbinds the render target and frees the slot.
	//------------------------------------------------------------------------------------------------
	void StopFaded()
	{
		if (!m_bActive)
		{
			Stop();
			return;
		}

		if (m_bFading && m_bFadingOut)
			return;

		BeginFade(true);
	}

	//------------------------------------------------------------------------------------------------
	bool IsFadingOut()
	{
		return m_bFading && m_bFadingOut;
	}

	//------------------------------------------------------------------------------------------------
	// Start a fade in the given direction from wherever the current one got to.
	//
	// Deliberately does NOT reset progress. Toggling twice quickly should reverse from where the
	// panel actually is, not snap to one end and run the whole way back.
	//------------------------------------------------------------------------------------------------
	protected void BeginFade(bool fadeOut)
	{
		m_bFading = true;
		m_bFadingOut = fadeOut;

		if (!GetGame() || !GetGame().GetCallqueue())
		{
			// No callqueue to drive it. Land on the end state rather than leaving the panel stuck
			// at whatever opacity it happened to be at.
			FinishFade();
			return;
		}

		GetGame().GetCallqueue().Remove(FadeTick);
		GetGame().GetCallqueue().CallLater(FadeTick, FADE_TICK_MS, true);
	}

	//------------------------------------------------------------------------------------------------
	protected void CancelFade()
	{
		m_bFading = false;
		m_bFadingOut = false;

		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().Remove(FadeTick);
	}

	//------------------------------------------------------------------------------------------------
	protected void FadeTick()
	{
		if (!m_bFading)
		{
			CancelFade();
			return;
		}

		// The widget went away underneath the fade - a hard Stop from somewhere else. Nothing left
		// to fade, and continuing would tick forever against a null root.
		if (!m_wRoot)
		{
			CancelFade();
			return;
		}

		// A float local rather than an int with a cast at the division. C-style casts appear
		// nowhere else in this project, so there is no precedent here to say they compile - and
		// an int/int division silently flooring to zero would leave the panel frozen mid-fade.
		float duration = FADE_IN_MS;
		if (m_bFadingOut)
			duration = FADE_OUT_MS;

		float step = FADE_TICK_MS / duration;

		if (m_bFadingOut)
			m_fFadeProgress = m_fFadeProgress - step;
		else
			m_fFadeProgress = m_fFadeProgress + step;

		if (m_fFadeProgress <= 0 || m_fFadeProgress >= 1.0)
		{
			FinishFade();
			return;
		}

		ApplyFadeOpacity();
	}

	//------------------------------------------------------------------------------------------------
	// Land on the end state. A fade-out ending is what actually releases the feed.
	//------------------------------------------------------------------------------------------------
	protected void FinishFade()
	{
		bool wasFadingOut = m_bFadingOut;

		m_fFadeProgress = 1.0;
		if (wasFadingOut)
			m_fFadeProgress = 0;

		CancelFade();

		if (wasFadingOut)
		{
			// Stop() tears the widgets down, so there is nothing left to apply an opacity to and
			// no point setting one first.
			Stop();
			return;
		}

		ApplyFadeOpacity();
	}

	//------------------------------------------------------------------------------------------------
	// Applied to the ROOT so the frame, the readouts and the picture all fade as one thing.
	//
	// Opacity in Enfusion multiplies down the hierarchy, so this is one call rather than one per
	// widget - and it means the resting translucency and the fade are the same number rather than
	// two effects that have to agree with each other.
	//------------------------------------------------------------------------------------------------
	protected void ApplyFadeOpacity()
	{
		if (!m_wRoot)
			return;

		m_wRoot.SetOpacity(Math.Clamp(m_fFadeProgress, 0, 1) * FEED_OPACITY);
	}

	//------------------------------------------------------------------------------------------------
	// Carrying two of the same device gives two listeners and two events per press. An even number
	// of repeats cancels out, so a double toggle does nothing and a double cycle never changes
	// source.
	protected bool IsRepeatInput(inout float lastTime, string label)
	{
		float now = System.GetTickCount();
		float sinceLast = now - lastTime;

		if (lastTime > 0 && sinceLast < TOGGLE_DEBOUNCE_MS)
		{
			Print("[ACM RemoteFeed] " + label + " ignored - repeat " + sinceLast.ToString() + "ms after the last one. The input is firing more than once per press.", LogLevel.WARNING);
			return true;
		}

		lastTime = now;
		return false;
	}

	//------------------------------------------------------------------------------------------------
	void Toggle()
	{
		if (IsRepeatInput(m_fLastToggleTime, "Toggle"))
			return;

		// Mid fade-out counts as already off, so pressing toggle again means "actually, keep it"
		// and falls through to Start(), which reverses the fade rather than rebuilding the panel.
		if (m_bActive && !IsFadingOut())
		{
			StopFaded();
			return;
		}

		Start();
	}

	//------------------------------------------------------------------------------------------------
	bool IsActive()
	{
		return m_bActive;
	}

	//------------------------------------------------------------------------------------------------
	ACM_ERemoteFeedState GetState()
	{
		return m_eState;
	}

	//------------------------------------------------------------------------------------------------
	// Visor up is a hard stop. The device is off; no reason to keep a slot driven behind a hidden
	// widget.
	//------------------------------------------------------------------------------------------------
	void OnVisorChanged(bool down)
	{
		if (m_bVisorDown == down)
			return;

		m_bVisorDown = down;

		if (!down)
		{
			m_bWantedBeforeVisor = m_bActive;
			Stop();
			return;
		}

		if (m_bWantedBeforeVisor)
			Start();
	}

	//------------------------------------------------------------------------------------------------
	// Source selection.
	//
	// The contract is a transform, a field of view, an exposure scale and a callsign. The renderer
	// never asks what kind of thing the source is.
	//------------------------------------------------------------------------------------------------
	void SetSource(ACM_RemoteFeedSourceComponent source)
	{
		m_Source = source;

		// Do not inherit the last source's quality or dwell timers.
		m_Signal.Reset();
		m_iAppliedBand = -1;
		m_eAppliedLinkState = ACM_ERemoteFeedLinkState.NONE;

		if (m_Source)
		{
			m_fFieldOfView = m_Source.GetFieldOfView();
			m_fExposureScale = m_Source.GetExposureScale();
		}

		UpdateTitle();
	}

	//------------------------------------------------------------------------------------------------
	ACM_RemoteFeedSourceComponent GetSource()
	{
		return m_Source;
	}

	//------------------------------------------------------------------------------------------------
	// Input-facing. Internal callers use CycleSource directly so a start-up selection or a destroyed
	// source is never swallowed by the debounce.
	void CycleSourceInput()
	{
		if (IsRepeatInput(m_fLastCycleTime, "Cycle"))
			return;

		CycleSource();
	}

	//------------------------------------------------------------------------------------------------
	void CycleSource()
	{
		// Filtered at the list, so an unauthorised source is never cycled onto or evaluated.
		int count = ACM_RemoteFeedSourceComponent.GetAuthorisedSources(SCR_PlayerController.GetLocalPlayerId(), m_aSourceScratch);
		if (count == 0)
		{
			m_Source = null;
			UpdateTitle();
			if (ACM_FEED_DEBUG)
				Print("[ACM RemoteFeed] No feed sources available. Falling back to the test camera.", LogLevel.DEBUG);
			return;
		}

		int current = -1;
		if (m_Source)
			current = m_aSourceScratch.Find(m_Source);

		int next = current + 1;
		if (next >= count)
			next = 0;

		SetSource(m_aSourceScratch[next]);

		// List everything on offer, not just what was picked. "It cycled to the wrong thing" and
		// "it never saw the thing" look identical from one line, and they have different fixes.
		if (ACM_FEED_DEBUG)
			Print("[ACM RemoteFeed] Cycled to source " + (next + 1).ToString() + " of " + count.ToString() + ": " + m_Source.GetCallsign(), LogLevel.DEBUG);

		for (int i = 0; i < count; i++)
		{
			ACM_RemoteFeedSourceComponent listed = m_aSourceScratch[i];

			string marker = "   ";
			if (i == next)
				marker = " > ";

			vector where;
			string position = "no usable position";
			if (listed.GetDiagnosticPosition(where))
				position = where.ToString();

			if (ACM_FEED_DEBUG)
				Print("[ACM RemoteFeed]" + marker + (i + 1).ToString() + ". " + listed.GetCallsign() + " at " + position, LogLevel.DEBUG);
		}
	}

	//------------------------------------------------------------------------------------------------
	// The other direction. Same list, same authorisation filter - a picker that walked its own list
	// would be a second place for the authorisation rule to live, and the second place is the one
	// that ends up wrong.
	//------------------------------------------------------------------------------------------------
	void CyclePrevSource()
	{
		int count = ACM_RemoteFeedSourceComponent.GetAuthorisedSources(SCR_PlayerController.GetLocalPlayerId(), m_aSourceScratch);
		if (count == 0)
		{
			m_Source = null;
			UpdateTitle();
			return;
		}

		int current = -1;
		if (m_Source)
			current = m_aSourceScratch.Find(m_Source);

		int previous = current - 1;
		if (previous < 0)
			previous = count - 1;

		SetSource(m_aSourceScratch[previous]);
	}

	//------------------------------------------------------------------------------------------------
	// For the radial source picker. Fills the caller's array with the sources this viewer is
	// authorised to see, in the same order the cycle walks, so the picker's [ACTIVE] mark and the
	// Next button agree with each other.
	//------------------------------------------------------------------------------------------------
	static int GetSelectableSources(out notnull array<ACM_RemoteFeedSourceComponent> outSources)
	{
		return ACM_RemoteFeedSourceComponent.GetAuthorisedSources(SCR_PlayerController.GetLocalPlayerId(), outSources);
	}

	//------------------------------------------------------------------------------------------------
	// Selection from the picker. Goes through the same authorisation list rather than trusting the
	// handle it was given - a menu entry built one frame can be performed several frames later, by
	// which time the source may have moved out of the viewer's squad, been unlinked, or died.
	//------------------------------------------------------------------------------------------------
	bool SelectSource(ACM_RemoteFeedSourceComponent wanted)
	{
		if (!wanted)
			return false;

		int count = ACM_RemoteFeedSourceComponent.GetAuthorisedSources(SCR_PlayerController.GetLocalPlayerId(), m_aSourceScratch);
		if (count == 0 || m_aSourceScratch.Find(wanted) == -1)
			return false;

		SetSource(wanted);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	// EXTERNAL PRESENTATION SUPPRESSION
	//
	// A generic seam for compatibility addons: something else owns the screen, stand down. Core
	// never calls it and never checks who did.
	//------------------------------------------------------------------------------------------------
	static void SetExternallySuppressed(bool suppressed, string reason = "")
	{
		s_bExternallySuppressed = suppressed;
		s_sSuppressionReason = reason;

		if (!s_Instance)
			return;

		s_Instance.ApplySuppression(suppressed);
	}

	//------------------------------------------------------------------------------------------------
	// Same shape as the visor: remember what the wearer wanted, take the feed down, put it back the
	// way it was when the suppression lifts. Never silently leaves the feed on or off.
	//------------------------------------------------------------------------------------------------
	protected void ApplySuppression(bool suppressed)
	{
		if (suppressed)
		{
			if (!m_bActive)
				return;

			m_bWantedBeforeSuppression = true;
			Stop();
			return;
		}

		if (!m_bWantedBeforeSuppression)
			return;

		m_bWantedBeforeSuppression = false;

		// Visor still has the final say. Coming back from suppression must not raise a feed on a
		// device that is switched off.
		if (m_bVisorDown)
			Start();
	}

	//------------------------------------------------------------------------------------------------
	static bool IsExternallySuppressed()
	{
		return s_bExternallySuppressed;
	}

	//------------------------------------------------------------------------------------------------
	static string GetSuppressionReason()
	{
		return s_sSuppressionReason;
	}

	//------------------------------------------------------------------------------------------------
	// Called by a source as it is destroyed. Step on rather than blank, so a destroyed drone does
	// not take the feed down with it.
	//------------------------------------------------------------------------------------------------
	void OnSourceRemoved(ACM_RemoteFeedSourceComponent source)
	{
		if (m_Source != source)
			return;

		m_Source = null;

		if (m_bActive)
			CycleSource();
	}

	//------------------------------------------------------------------------------------------------
	// Handed a matrix, never an entity, so the renderer cannot grow a dependency on the source.
	//------------------------------------------------------------------------------------------------
	void SetFeedTransform(vector mat[4])
	{
		m_mFeedTransform[0] = mat[0];
		m_mFeedTransform[1] = mat[1];
		m_mFeedTransform[2] = mat[2];
		m_mFeedTransform[3] = mat[3];
		m_bHasTransform = true;
	}

	//------------------------------------------------------------------------------------------------
	// Everything the camera does: where it is, how wide it sees, how bright it is. Per-frame, which
	// is what makes the picture follow a moving source without any tracking code.
	//------------------------------------------------------------------------------------------------
	protected void DriveTick()
	{
		if (!m_bActive)
			return;

		// The bind can only happen once the widget has been laid out, which is not guaranteed on
		// the frame it was created - so keep trying until it takes, and drive nothing until then.
		if (!m_bTargetBound)
		{
			if (!AttachRenderTarget())
				return;
		}

		EvaluateSignal();

		// A lost link renders nothing - no point driving a camera nobody can watch.
		if (m_Signal.GetState() == ACM_ERemoteFeedLinkState.LOST)
			return;

		if (m_eState != ACM_ERemoteFeedState.RENDERING)
			return;

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return;

		// The Game Master editor drives its own cameras - stay out of its way.
		if (SCR_EditorManagerEntity.IsOpenedInstance())
			return;

		if (!ResolveTransform())
			return;

		world.SetCameraEx(m_iCamSlot, m_mFeedTransform);
		world.SetCameraVerticalFOV(m_iCamSlot, m_fFieldOfView);

		ApplyExposure(world);
	}

	//------------------------------------------------------------------------------------------------
	protected bool ResolveTransform()
	{
		if (m_Source && m_Source.IsAvailable())
		{
			vector mat[4];
			if (m_Source.GetFeedTransform(mat))
			{
				SetFeedTransform(mat);
				return true;
			}
		}

		UpdateTestTransform();
		return m_bHasTransform;
	}

	//------------------------------------------------------------------------------------------------
	// A render-target camera gets no auto-exposure, so without this the feed reads black or blows
	// out. Copy the main view's and scale it - an absolute value pushes it to white.
	//------------------------------------------------------------------------------------------------
	protected void ApplyExposure(BaseWorld world)
	{
		if (m_fExposureScale <= 0)
			return;

		int mainCam = 0;
		CameraManager manager = GetGame().GetCameraManager();
		if (manager)
		{
			CameraBase current = manager.CurrentCamera();
			if (current)
				mainCam = current.GetCameraIndex();
		}

		world.SetCameraHDRBrightness(m_iCamSlot, world.GetCameraHDRBrightness(mainCam) * m_fExposureScale);
	}

	// A camera slot has no post-process chain of its own, so without this the feed renders raw while
	// the wearer is looking through a green tube.
	//
	// Version counter rather than re-applying every tick - four effects at 10Hz to say nothing
	// changed is pure waste.
	//------------------------------------------------------------------------------------------------
	protected void SyncNightVision()
	{
		if (!m_bTargetBound)
			return;

		int version = ACM_NightVisionComponent.GetEffectVersion();
		if (version == m_iMirroredNvgVersion)
			return;

		m_iMirroredNvgVersion = version;

		if (ACM_NightVisionComponent.MirrorEffectsTo(m_iCamSlot))
		{
			if (ACM_FEED_DEBUG)
				Print("[ACM RemoteFeed] Night vision mirrored onto camera slot " + m_iCamSlot.ToString() + ".", LogLevel.DEBUG);
			return;
		}

		// Nothing active - goggles up. The feed goes clean rather than keeping a stale tint.
		ApplyInterference(0);
		ACM_NightVisionComponent.ClearMirroredEffects(m_iCamSlot);
		if (ACM_FEED_DEBUG)
			Print("[ACM RemoteFeed] Night vision inactive - feed camera cleared.", LogLevel.DEBUG);
	}

	// The evaluator decided the number. This decides what it looks like.
	//------------------------------------------------------------------------------------------------
	protected void EvaluateSignal()
	{
		// Forced quality bypasses the evaluator entirely - including the "no source" case, so the
		// visuals can be checked on the built-in test camera.
		if (m_fDebugQuality >= 0)
		{
			m_Signal.ForceQuality(m_fDebugQuality, System.GetTickCount() / 1000.0);
			ApplyLinkState();
			LogLink();
			return;
		}

		if (!m_Source)
			return;

		vector viewerPos;
		IEntity viewer = SCR_PlayerController.GetLocalControlledEntity();
		if (viewer)
			viewerPos = viewer.GetOrigin();

		vector sourcePos;
		if (!m_Source.GetLinkOrigin(sourcePos))
			return;

		// One hop today. The array is the relay seam: a relayed link becomes an extra element
		// rather than a new signature on every caller.
		m_aLinkPath.Clear();
		m_aLinkPath.Insert(sourcePos);

		if (!m_Signal.Evaluate(viewerPos, m_aLinkPath, m_Source, System.GetTickCount()))
			return;

		ApplyLinkState();
		LogLink();
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyLinkState()
	{
		ACM_ERemoteFeedLinkState linkState = m_Signal.GetState();

		if (linkState != m_eAppliedLinkState)
		{
			m_eAppliedLinkState = linkState;

			if (linkState == ACM_ERemoteFeedLinkState.LOST)
				EnterSignalLost();
			else if (linkState == ACM_ERemoteFeedLinkState.RECONNECTING)
				EnterReconnecting();
			else
				EnterLive();
		}

		if (linkState == ACM_ERemoteFeedLinkState.LOST)
		{
			UpdateLinkReadout();
			return;
		}

		// Continuous, not banded. Pixelation is the most legible cue there is and stepping it four
		// times between perfect and dead wastes that. It is a widget property, so it costs nothing.
		ApplyPixelation();

		// Continuous too - intensity is a number, no reason to quantise it into five steps.
		ApplyInterference(InterferenceFor(m_Signal.GetQuality()));

		ApplyBand();
		UpdateLinkReadout();
	}

	//------------------------------------------------------------------------------------------------
	// Reapplied only on change. Setting frame rate three times a second to say nothing moved is the
	// sort of waste that hides until something else is slow.
	//------------------------------------------------------------------------------------------------
	protected void ApplyBand()
	{
		if (!m_wRenderTarget)
			return;

		array<ref ACM_RemoteFeedBand> bands = Bands();
		float quality = m_Signal.GetQuality();

		int index = bands.Count() - 1;
		for (int i = 0; i < bands.Count(); i++)
		{
			if (quality >= bands[i].minQuality)
			{
				index = i;
				break;
			}
		}

		if (index == m_iAppliedBand)
			return;

		m_iAppliedBand = index;
		ACM_RemoteFeedBand band = bands[index];

		// Native controls, so a degraded feed genuinely costs less rather than costing the same
		// under a layer of noise.
		// Frame rate is the only thing still banded - resolution and interference are continuous.
		m_wRenderTarget.SetMaxFPS(band.maxFPS);

		if (ACM_FEED_DEBUG)
			Print("[ACM RemoteFeed] Link band " + index.ToString() + " at " + m_Signal.GetLinkPercent().ToString() + "% - " + band.maxFPS.ToString() + " fps, interference " + band.interference.ToString(), LogLevel.DEBUG);
	}

	// Once a second. "Not degrading" and "degrading, but the link really is at 100%" look identical
	// on screen.
	//------------------------------------------------------------------------------------------------
	protected void LogLink()
	{
		float now = System.GetTickCount();
		if (m_fLastLinkLog > 0 && now - m_fLastLinkLog < 1000)
			return;

		m_fLastLinkLog = now;

		string sourceName = "test camera";
		float distance = 0;

		if (m_Source)
		{
			sourceName = m_Source.GetCallsign();

			vector sourcePos;
			IEntity viewer = SCR_PlayerController.GetLocalControlledEntity();
			if (viewer && m_Source.GetLinkOrigin(sourcePos))
				distance = vector.Distance(viewer.GetOrigin(), sourcePos);
		}

		if (ACM_FEED_DEBUG)
			Print("[ACM RemoteFeed] LINK " + m_Signal.GetLinkPercent().ToString() + "% from " + sourceName + " at " + Math.Round(distance).ToString() + "m, state " + SCR_Enum.GetEnumName(ACM_ERemoteFeedLinkState, m_Signal.GetState()) + ", obstruction " + Math.Round(m_Signal.GetObstructionFraction() * 100).ToString() + "%, band " + m_iAppliedBand.ToString() + ", resScale " + m_fAppliedResolution.ToString() + ", grain " + m_fAppliedInterference.ToString(), LogLevel.DEBUG);
	}

	//------------------------------------------------------------------------------------------------
	void SetDebugQuality(float quality)
	{
		m_fDebugQuality = quality;

		if (quality < 0)
			m_Signal.Reset();

		m_iAppliedBand = -1;
	}

	//------------------------------------------------------------------------------------------------
	// Curved rather than linear, so a strong link stays sharp and the coarsening arrives with the
	// trouble instead of the first metre of movement.
	//------------------------------------------------------------------------------------------------
	protected void ApplyPixelation()
	{
		if (!m_wRenderTarget)
			return;

		float quality = m_Signal.GetQuality();
		float shaped = Math.Pow(quality, RESOLUTION_CURVE_POWER);

		float scale = MIN_RESOLUTION_SCALE + ((1.0 - MIN_RESOLUTION_SCALE) * shaped);
		scale = scale * m_fRenderScale;

		if (scale < 0.02)
			scale = 0.02;

		if (scale > 1.0)
			scale = 1.0;

		m_wRenderTarget.SetResolutionScale(scale, scale);
		m_fAppliedResolution = scale;
	}

	//------------------------------------------------------------------------------------------------
	// Grain on the feed camera ONLY, at a priority night vision does not use. No second camera and
	// no extra render pass - the interference is a post-process on the one camera that already
	// exists.
	//------------------------------------------------------------------------------------------------
	// Squared, so noise arrives with the trouble rather than creeping in from the first metre.
	//------------------------------------------------------------------------------------------------
	protected float InterferenceFor(float quality)
	{
		float amount = 1.0 - quality;
		return amount * amount;
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyInterference(float amount)
	{
		BaseWorld world = GetGame().GetWorld();
		if (!world || m_iCamSlot < 0)
			return;

		// Binding only changes at the on/off boundary; strength is a material parameter.
		bool wantsEffect = amount > 0.02;
		bool hadEffect = m_fAppliedInterference > 0.02;

		if (wantsEffect != hadEffect || m_fAppliedInterference < 0)
		{
			if (wantsEffect)
			{
				world.SetCameraPostProcessEffect(m_iCamSlot, PP_PRIORITY_SIGNAL, PostProcessEffectType.FilmGrain, SIGNAL_GRAIN);
				world.SetCameraPostProcessEffect(m_iCamSlot, PP_PRIORITY_SIGNAL_HEAVY, PostProcessEffectType.FilmGrain, SIGNAL_BLEMISH);
			}
			else
			{
				world.SetCameraPostProcessEffect(m_iCamSlot, PP_PRIORITY_SIGNAL, PostProcessEffectType.None, "");
				world.SetCameraPostProcessEffect(m_iCamSlot, PP_PRIORITY_SIGNAL_HEAVY, PostProcessEffectType.None, "");
			}
		}

		m_fAppliedInterference = amount;

		if (!wantsEffect)
			return;

		DriveGrain(amount);
	}

	//------------------------------------------------------------------------------------------------
	// Drives the material's parameters - the only way to get a range out of one asset.
	//
	// Materials are shared. If an NVG device is ever authored to use ACM_WhiteGrain, both systems
	// will be writing the same parameters. The separation is a convention, not a guarantee.
	//------------------------------------------------------------------------------------------------
	protected void DriveGrain(float amount)
	{
		if (!m_SignalGrain)
			m_SignalGrain = Material.GetOrLoadMaterial(SIGNAL_GRAIN, 0);

		if (!m_SignalGrain)
			return;

		float intensity = GRAIN_INTENSITY_MIN + ((GRAIN_INTENSITY_MAX - GRAIN_INTENSITY_MIN) * amount);
		float size = GRAIN_SIZE_MIN + ((GRAIN_SIZE_MAX - GRAIN_SIZE_MIN) * amount);

		m_SignalGrain.SetParam("Intensity", intensity);
		m_SignalGrain.SetParam("GrainSize", size);
	}

	//------------------------------------------------------------------------------------------------
	// Put it back. Left at storm intensity it would follow anything else using this material.
	//------------------------------------------------------------------------------------------------
	protected void ReleaseGrain()
	{
		if (!m_SignalGrain)
			return;

		m_SignalGrain.ResetParam("Intensity");
		m_SignalGrain.ResetParam("GrainSize");
		m_SignalGrain = null;
		m_fAppliedInterference = -1;
	}

	//------------------------------------------------------------------------------------------------
	// Keep the widgets allocated. Rebuilding the feed every time somebody walks past a wall costs
	// far more than parking it.
	//------------------------------------------------------------------------------------------------
	protected void EnterSignalLost()
	{
		if (m_wRenderTarget)
		{
			m_wRenderTarget.SetRefresh(PARKED_REFRESH_PERIOD, 0);
			m_wRenderTarget.SetMaxFPS(1);
		}

		ApplyInterference(1.0);
		m_iAppliedBand = -1;

		if (ACM_FEED_DEBUG)
			Print("[ACM RemoteFeed] SIGNAL LOST - render updates suspended, last frame held.", LogLevel.DEBUG);
	}

	//------------------------------------------------------------------------------------------------
	protected void EnterReconnecting()
	{
		if (m_wRenderTarget)
			m_wRenderTarget.SetRefresh(NORMAL_REFRESH_PERIOD, 0);

		// No jump to a clean picture - the band that applies is the one the quality earns.
		m_iAppliedBand = -1;

		if (ACM_FEED_DEBUG)
			Print("[ACM RemoteFeed] RECONNECTING at " + m_Signal.GetLinkPercent().ToString() + "%.", LogLevel.DEBUG);
	}

	//------------------------------------------------------------------------------------------------
	protected void EnterLive()
	{
		if (m_wRenderTarget)
			m_wRenderTarget.SetRefresh(NORMAL_REFRESH_PERIOD, 0);

		m_iAppliedBand = -1;
	}

	//------------------------------------------------------------------------------------------------
	// LINK is connection quality, OPERATOR is who controls the source. Different facts, no shared
	// wording.
	//------------------------------------------------------------------------------------------------
	protected void UpdateLinkReadout()
	{
		if (!m_wLink)
			return;

		if (!m_Source)
		{
			m_wLink.SetText("");
			return;
		}

		ACM_ERemoteFeedLinkState linkState = m_Signal.GetState();

		if (linkState == ACM_ERemoteFeedLinkState.LOST)
		{
			m_wLink.SetText("SIGNAL LOST");
			return;
		}

		string line = "LINK: " + m_Signal.GetLinkPercent().ToString() + "%";

		if (linkState == ACM_ERemoteFeedLinkState.RECONNECTING)
		{
			m_wLink.SetText(line + "  RECONNECTING");
			return;
		}

		int band = m_iAppliedBand;
		if (band >= 0 && band < Bands().Count())
		{
			string label = Bands()[band].label;
			if (label != string.Empty)
				line = line + "  " + label;
		}

		m_wLink.SetText(line);
	}

	//------------------------------------------------------------------------------------------------
	// Arbitration
	//------------------------------------------------------------------------------------------------
	void SetArbitration(ACM_ERemoteFeedArbitration mode)
	{
		if (m_eArbitration == mode)
			return;

		m_eArbitration = mode;
		Evaluate();
	}

	//------------------------------------------------------------------------------------------------
	ACM_ERemoteFeedArbitration GetArbitration()
	{
		return m_eArbitration;
	}

	//------------------------------------------------------------------------------------------------
	protected void ArbitrationTick()
	{
		if (!m_bActive)
			return;

		UpdateOwner(false);
		SyncNightVision();

		bool opticNow = SCR_2DPIPSightsComponent.IsPIPActive();
		if (opticNow == m_bOpticActive)
			return;

		m_bOpticActive = opticNow;

		// If the optic uses our slot, whichever binds last wins and it looks like the engine
		// refusing two feeds. Name it rather than let it masquerade as the answer.
		if (opticNow)
		{
			SCR_2DPIPSightsComponent sights = ArmaReforgerScripted.GetCurrentPIPSights();
			if (sights)
			{
				int opticSlot = sights.GetPIPCameraIndex();
				if (opticSlot == m_iCamSlot)
					Print("[ACM RemoteFeed] COLLISION: the weapon optic uses camera slot " + opticSlot.ToString() + ", which is ours. Move the feed to another slot in " + CAM_SLOT_MIN.ToString() + "-" + CAM_SLOT_MAX.ToString() + " before trusting any concurrency result.", LogLevel.ERROR);
				else
					if (ACM_FEED_DEBUG)
						Print("[ACM RemoteFeed] Optic active on camera slot " + opticSlot.ToString() + "; feed is on " + m_iCamSlot.ToString() + ". No collision.", LogLevel.DEBUG);
			}
		}

		Evaluate();
	}

	//------------------------------------------------------------------------------------------------
	// Single place where requested state plus arbitration resolve into actual state.
	//------------------------------------------------------------------------------------------------
	protected void Evaluate()
	{
		if (!m_bActive || !m_wRenderTarget)
			return;

		bool suspend = false;
		if (m_eArbitration == ACM_ERemoteFeedArbitration.SUSPEND_ON_ADS)
			suspend = m_bOpticActive;

		if (suspend)
		{
			Park();
			return;
		}

		Resume();
	}

	//------------------------------------------------------------------------------------------------
	// Hold the last frame, stop refreshing, stop touching the camera slot.
	//------------------------------------------------------------------------------------------------
	protected void Park()
	{
		if (m_eState == ACM_ERemoteFeedState.SUSPENDED)
			return;

		m_wRenderTarget.SetRefresh(PARKED_REFRESH_PERIOD, 0);
		m_wRenderTarget.SetMaxFPS(1);

		if (m_wStatus)
		{
			m_wStatus.SetText("OPTIC PRIORITY");
			m_wStatus.SetVisible(true);
		}

		m_eState = ACM_ERemoteFeedState.SUSPENDED;

		if (ACM_FEED_DEBUG)
			Print("[ACM RemoteFeed] Parked - optic priority. Last frame held.", LogLevel.DEBUG);
	}

	//------------------------------------------------------------------------------------------------
	protected void Resume()
	{
		if (m_eState == ACM_ERemoteFeedState.RENDERING)
			return;

		m_wRenderTarget.SetRefresh(NORMAL_REFRESH_PERIOD, 0);
		m_wRenderTarget.SetMaxFPS(m_iMaxFPS);

		if (m_wStatus)
			m_wStatus.SetVisible(false);

		m_eState = ACM_ERemoteFeedState.RENDERING;

		if (ACM_FEED_DEBUG)
			Print("[ACM RemoteFeed] Resumed at " + m_iMaxFPS.ToString() + " fps.", LogLevel.DEBUG);
	}

	//------------------------------------------------------------------------------------------------
	// Widgets
	//------------------------------------------------------------------------------------------------
	protected bool CreateWidgets()
	{
		if (m_wRoot)
			return true;

		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if (!workspace)
			return false;

		m_wRoot = workspace.CreateWidgets(FEED_LAYOUT);
		if (!m_wRoot)
		{
			Print("[ACM RemoteFeed] Feed layout failed to load. Open the addon in Workbench once so UI/layouts/HUD/ACM_RemoteFeed.layout is registered in the resource database.", LogLevel.WARNING);
			return false;
		}

		m_wPanel = m_wRoot.FindAnyWidget("ACM_RemoteFeedPanel");
		m_wRenderTarget = RenderTargetWidget.Cast(m_wRoot.FindAnyWidget("ACM_RemoteFeedTarget"));
		m_wCanvas = CanvasWidget.Cast(m_wRoot.FindAnyWidget("ACM_RemoteFeedCanvas"));
		m_wTitle = TextWidget.Cast(m_wRoot.FindAnyWidget("ACM_RemoteFeedTitle"));
		m_wStatus = TextWidget.Cast(m_wRoot.FindAnyWidget("ACM_RemoteFeedStatus"));
		m_wOwner = TextWidget.Cast(m_wRoot.FindAnyWidget("ACM_RemoteFeedOwner"));
		m_wLink = TextWidget.Cast(m_wRoot.FindAnyWidget("ACM_RemoteFeedLink"));

		if (!m_wRenderTarget || !m_wPanel)
		{
			Print("[ACM RemoteFeed] Layout is missing ACM_RemoteFeedPanel or ACM_RemoteFeedTarget.", LogLevel.ERROR);
			DestroyWidgets();
			return false;
		}

		ApplyGeometry(workspace);
		ApplyRenderSettings();
		AttachRenderTarget();

		if (m_wStatus)
			m_wStatus.SetVisible(false);

		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected void DestroyWidgets()
	{
		if (m_wRoot)
			m_wRoot.RemoveFromHierarchy();

		m_wRoot = null;
		m_wPanel = null;
		m_wCanvas = null;
		m_wRenderTarget = null;
		m_wTitle = null;
		m_wStatus = null;
		m_wOwner = null;
		m_wLink = null;
		m_iShownOwnerId = -1;
	}

	//------------------------------------------------------------------------------------------------
	protected void UpdateTitle()
	{
		if (!m_wTitle)
			return;

		if (!m_Source)
		{
			m_wTitle.SetText("REMOTE FEED // TEST");
			return;
		}

		m_wTitle.SetText("REMOTE FEED // " + m_Source.GetCallsign());
		UpdateOwner(true);
	}

	//------------------------------------------------------------------------------------------------
	// Re-resolved on the tick, not just on selection - drones get handed over and handlers get
	// killed. Only rewritten when the id changes, so the usual case is one integer compare.
	//------------------------------------------------------------------------------------------------
	protected void UpdateOwner(bool force)
	{
		if (!m_wOwner)
			return;

		if (!m_Source)
		{
			if (force || m_iShownOwnerId != 0)
			{
				m_iShownOwnerId = 0;
				m_wOwner.SetText("");
			}

			return;
		}

		int ownerId = m_Source.GetControllerPlayerId();
		if (!force && ownerId == m_iShownOwnerId)
			return;

		m_iShownOwnerId = ownerId;

		string line = m_Source.GetOwnerRole() + ": " + m_Source.GetControllerDisplayName();
		if (m_Source.IsLocallyControlled())
			line = line + "  (YOU)";

		m_wOwner.SetText(line);
	}

	// Markers drawn into the feed.
	//
	// OutlineEntity is a main-view pass and does not reach a render-target camera, so the feed has
	// to draw its own marks. The target detection system does that work; this exposes the surface
	// and the numbers it needs.
	//------------------------------------------------------------------------------------------------
	CanvasWidget GetFeedCanvas()
	{
		return m_wCanvas;
	}

	// Where the feed camera is and which way it looks, for a sensor run from its point of view.
	//------------------------------------------------------------------------------------------------
	bool GetFeedCameraTransform(out vector outMat[4])
	{
		if (!m_bHasTransform)
			return false;

		outMat[0] = m_mFeedTransform[0];
		outMat[1] = m_mFeedTransform[1];
		outMat[2] = m_mFeedTransform[2];
		outMat[3] = m_mFeedTransform[3];

		return true;
	}

	//------------------------------------------------------------------------------------------------
	int GetCamSlot()
	{
		return m_iCamSlot;
	}

	//------------------------------------------------------------------------------------------------
	// Only while actually drawing - a parked feed should not have markers computed for it.
	//------------------------------------------------------------------------------------------------
	bool IsRenderingFeed()
	{
		if (!m_bActive || m_eState != ACM_ERemoteFeedState.RENDERING)
			return false;

		return m_wCanvas != null;
	}

	//------------------------------------------------------------------------------------------------
	// The panel's rect in screen pixels, which is where a projection through the feed camera has
	// to land.
	//------------------------------------------------------------------------------------------------
	bool GetFeedSurfaceSize(out float outW, out float outH)
	{
		// Size only, no screen position. Canvas commands are relative to the canvas widget's own
		// top-left. A full-screen canvas makes the two identical, which is why this is easy to miss.
		if (!m_wCanvas || !m_bTargetBound)
			return false;

		m_wCanvas.GetScreenSize(outW, outH);

		return outW > 0 && outH > 0;
	}

	//------------------------------------------------------------------------------------------------
	float GetFieldOfView()
	{
		return m_fFieldOfView;
	}

	//------------------------------------------------------------------------------------------------
	// Computed in screen pixels, converted to reference units at the point of use. Frame slots are
	// in reference units, display regions in real pixels - mixing them gives the right shape at the
	// wrong size.
	//------------------------------------------------------------------------------------------------
	// Placement used to be the layout's job, and it no longer can be. The layout cannot express a
	// device's optic region, because that is authored per device - and it cannot express the player's
	// size and corner presets either, because those are chosen at runtime from the radial. So the
	// layout now contributes the panel's ASPECT and nothing else, and everything about where the
	// panel sits and how big it is is computed here.
	//------------------------------------------------------------------------------------------------
	// How much of the top and bottom of the screen the visor is currently occupying, in PIXELS.
	//
	// Zero on both when the visor is up, so a device with no visor - or a wearer who has raised it -
	// gets exactly the placement it always got. Nothing about the feed changes for anyone who is not
	// looking through an IVAS.
	//
	// The numbers are the visor layout's own furniture measured in reference units and scaled here.
	// They are stated rather than queried because the layout is authored, not computed: asking the
	// widgets for their extents every time the feed is placed would be a live dependency on a
	// hierarchy that may not exist yet, to learn four numbers that only change when somebody edits
	// the .layout. If that layout moves, these move with it.
	//
	// TOP    bearing ribbon 26..70, heading readout 74..98, callsign plate 52..108.
	// BOTTOM readout plate -156..-52, event log plate -192..-52. The log is the deeper of the two,
	//        and one number for both corners is worth more than four units of reclaimed screen.
	protected static const float VISOR_SAFE_TOP = 116.0;
	protected static const float VISOR_SAFE_BOTTOM = 202.0;

	// The squad strip is centred on the LEFT edge - plate -140..+63 either side of screen centre -
	// so a left-docked feed has less room than a right-docked one. Only these two numbers are
	// side-specific; everything else about the safe area applies to both.
	protected static const float VISOR_SQUAD_ABOVE = 148.0;
	protected static const float VISOR_SQUAD_BELOW = 71.0;

	//------------------------------------------------------------------------------------------------
	protected bool IsLeftCorner()
	{
		return m_eCorner == ACM_ERemoteFeedCorner.TOP_LEFT || m_eCorner == ACM_ERemoteFeedCorner.BOTTOM_LEFT;
	}

	//------------------------------------------------------------------------------------------------
	protected void GetVisorSafeInsets(WorkspaceWidget workspace, out float top, out float bottom)
	{
		top = 0;
		bottom = 0;

		if (!workspace || !ACM_IvasVisorOverlay.IsShown())
			return;

		top = workspace.DPIScale(VISOR_SAFE_TOP);
		bottom = workspace.DPIScale(VISOR_SAFE_BOTTOM);
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyGeometry(WorkspaceWidget workspace)
	{
		if (!m_wPanel)
			return;

		// No device has declared a region yet. Screen-docked is the right fallback, not "do
		// nothing" - doing nothing would leave the size and corner presets inert until the first
		// region push, which from the player's side is a menu entry that does not work.
		bool docked = true;
		if (m_Region)
			docked = m_Region.shape == ACM_ERemoteFeedRegionShape.FULLSCREEN;

		float screenW = workspace.GetWidth();
		float screenH = workspace.GetHeight();
		if (screenW <= 0 || screenH <= 0)
			return;

		// Aspect comes from whatever the layout authored, so resizing the panel in the UI editor
		// still governs the shape of the picture.
		float panelW, panelH;
		m_wPanel.GetScreenSize(panelW, panelH);

		float aspect = 16.0 / 9.0;
		if (panelH > 0)
			aspect = panelW / panelH;

		// The bounds the panel is placed within: the device's optic region, or the whole screen
		// when no device declared one. Everything below is the same arithmetic either way, which is
		// what lets the size and corner presets work in both modes instead of only one.
		float rx = 0;
		float ry = 0;
		float rw = screenW;
		float rh = screenH;
		float sizeFraction = m_fScreenSizeFraction;
		float marginX = m_fScreenMargin;
		float marginY = m_fScreenMargin;

		if (!docked)
		{
			m_Region.GetUsableRect(screenW, screenH, rx, ry, rw, rh);
			sizeFraction = m_fRegionSizeFraction;
			marginX = m_fRegionMargin;
			marginY = m_fRegionMargin;
		}

		float wPx = rw * sizeFraction * m_fSizeScale;
		float hPx = wPx / aspect;

		// A wide, short region can run out of height before it runs out of width.
		float maxH = rh * 0.5;
		if (hPx > maxH)
		{
			hPx = maxH;
			wPx = hPx * aspect;
		}

		float insetX = rw * marginX;
		float insetY = rh * marginY;

		// VISOR SAFE AREA
		//
		// The IVAS visor owns fixed bands at the top and bottom of the screen - the bearing ribbon
		// and heading above, the event log and the weapon and grid readouts below - and it draws
		// over this panel, because it is created later and sits higher in the hierarchy.
		//
		// Docking to a corner without accounting for that put the feed straight on top of the
		// wearer's own callsign block. So the placement rect is shrunk while the visor is down and
		// restored when it comes up: the same corner preset, measured from the edge of the usable
		// picture rather than the edge of the screen.
		//
		// Deliberately applied to POSITION first and to SIZE only if the panel genuinely will not
		// fit. The wearer picked a feed size from the radial and it should not silently change
		// because they lowered a visor.
		float safeTop, safeBottom;
		GetVisorSafeInsets(workspace, safeTop, safeBottom);

		float sy = ry + safeTop;
		float sh = rh - safeTop - safeBottom;

		// A safe area that has eaten the whole region means something is very wrong with the
		// numbers rather than with the screen. Fall back to the unrestricted rect instead of
		// refusing to draw.
		if (sh < 64)
		{
			sy = ry;
			sh = rh;
		}

		// Left edge, while the squad strip is actually up. A LARGE feed docked top-left is tall
		// enough to reach the strip even after the top band has been taken out, so the band it may
		// occupy stops short of the strip rather than merely starting below the ribbon.
		//
		// Gated on the strip being SHOWN, not merely possible. Friendly IFF set to OFF, or nobody
		// nearby, and the wearer gets the whole left edge back - which is most of the time.
		if (ACM_IvasVisorOverlay.IsSquadStripShown() && IsLeftCorner())
		{
			float centreY = ry + (rh * 0.5);
			float squadTop = centreY - workspace.DPIScale(VISOR_SQUAD_ABOVE);
			float squadBottom = centreY + workspace.DPIScale(VISOR_SQUAD_BELOW);

			if (m_eCorner == ACM_ERemoteFeedCorner.TOP_LEFT)
			{
				float allowed = squadTop - sy;
				if (allowed > 64 && sh > allowed)
					sh = allowed;
			}
			else
			{
				float shift = squadBottom - sy;
				if (shift > 0 && (sh - shift) > 64)
				{
					sy = squadBottom;
					sh = sh - shift;
				}
			}
		}

		if (hPx > (sh - (insetY * 2)))
		{
			hPx = Math.Max(16, sh - (insetY * 2));
			wPx = hPx * aspect;
		}

		float x = rx + insetX;
		float y = sy + insetY;

		if (m_eCorner == ACM_ERemoteFeedCorner.TOP_RIGHT || m_eCorner == ACM_ERemoteFeedCorner.BOTTOM_RIGHT)
			x = rx + rw - insetX - wPx;

		if (m_eCorner == ACM_ERemoteFeedCorner.BOTTOM_LEFT || m_eCorner == ACM_ERemoteFeedCorner.BOTTOM_RIGHT)
			y = sy + sh - insetY - hPx;

		// A zero-sized render target is an illegal write that takes the process with it, so a
		// degenerate result is refused here rather than handed to the widget. The drive loop's own
		// guard would catch it a frame later; this stops it ever being written.
		if (wPx < 16 || hPx < 16)
		{
			Print("[ACM RemoteFeed] Refusing a " + wPx.ToString() + "x" + hPx.ToString() + " panel - size preset or display region is too small to render into.", LogLevel.WARNING);
			return;
		}

		FrameSlot.SetSize(m_wPanel, workspace.DPIUnscale(wPx), workspace.DPIUnscale(hPx));
		FrameSlot.SetPos(m_wPanel, workspace.DPIUnscale(x), workspace.DPIUnscale(y));

		if (ACM_FEED_DEBUG)
			Print("[ACM RemoteFeed] Placed " + wPx.ToString() + "x" + hPx.ToString() + " at " + x.ToString() + "," + y.ToString(), LogLevel.DEBUG);
	}

	//------------------------------------------------------------------------------------------------
	// Player placement, from the radial. Discrete presets rather than numbers, because these have
	// to be usable with a thumbstick.
	//
	// The scale multiplies the device's authored size rather than replacing it, so a set built with
	// a deliberately small window still reads as the small one at LARGE.
	//------------------------------------------------------------------------------------------------
	void SetSizeScale(float scale)
	{
		if (scale <= 0)
			return;

		m_fSizeScale = scale;

		if (m_wPanel && GetGame().GetWorkspace())
			ApplyGeometry(GetGame().GetWorkspace());
	}

	//------------------------------------------------------------------------------------------------
	// Re-run placement without changing any setting.
	//
	// The one caller is the visor going up or down: the safe area it reserves is part of the
	// placement arithmetic, so the panel has to be measured again when that area appears or goes
	// away. Everything else that moves the feed already re-places it as a side effect of the
	// setting it changed.
	void RefreshPlacement()
	{
		if (GetGame() && GetGame().GetWorkspace())
			ApplyGeometry(GetGame().GetWorkspace());
	}

	//------------------------------------------------------------------------------------------------
	void SetCorner(ACM_ERemoteFeedCorner corner)
	{
		m_eCorner = corner;

		if (m_wPanel && GetGame().GetWorkspace())
			ApplyGeometry(GetGame().GetWorkspace());
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyRenderSettings()
	{
		if (!m_wRenderTarget)
			return;

		// The render target's resolution is the panel's size times this scale, so a small panel at
		// half scale costs a quarter of the pixels. This is the performance knob, not the size.
		if (m_fRenderScale > 0 && m_fRenderScale <= 1)
			m_wRenderTarget.SetResolutionScale(m_fRenderScale, m_fRenderScale);

		m_wRenderTarget.SetMaxFPS(m_iMaxFPS);
		m_wRenderTarget.SetRefresh(NORMAL_REFRESH_PERIOD, 0);
	}

	//------------------------------------------------------------------------------------------------
	// Bind before anything expects a picture or the panel comes up empty.
	//------------------------------------------------------------------------------------------------
	protected bool AttachRenderTarget()
	{
		if (!m_wRenderTarget)
			return false;

		BaseWorld world = GetGame().GetWorld();
		if (!world)
			return false;

		// Pointing a camera at a zero-sized render target is an illegal write and takes the process
		// down with no script error. A widget also measures 0x0 on the frame it is created, before
		// layout runs, so this waits rather than fails.
		float targetW, targetH;
		m_wRenderTarget.GetScreenSize(targetW, targetH);

		if (targetW <= 0 || targetH <= 0)
		{
			if (!m_bWarnedZeroSize)
			{
				m_bWarnedZeroSize = true;
				Print("[ACM RemoteFeed] Render target has no size yet - deferring the camera bind. If this never clears, the layout's FrameWidgetSlot is missing its Offset values and the widget will stay 0x0.", LogLevel.WARNING);
			}

			return false;
		}

		m_wRenderTarget.SetWorld(world, m_iCamSlot);
		m_wRenderTarget.SetVisible(true);
		m_wRenderTarget.SetEnabled(true);

		m_bTargetBound = true;
		m_eState = ACM_ERemoteFeedState.RENDERING;

		m_iMirroredNvgVersion = -1;
		SyncNightVision();

		if (ACM_FEED_DEBUG)
			Print("[ACM RemoteFeed] Bound camera slot " + m_iCamSlot.ToString() + " to a " + targetW.ToString() + "x" + targetH.ToString() + " render target.", LogLevel.DEBUG);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected void DetachRenderTarget()
	{
		if (!m_wRenderTarget)
			return;

		m_wRenderTarget.SetVisible(false);
		m_wRenderTarget.SetWorld(null, -1);
	}

	//------------------------------------------------------------------------------------------------
	// Fallback when no source component exists anywhere. Aims along the player's own view at a fixed
	// height above them. Reads the local view camera, which belongs to the viewer, not to a source.
	//------------------------------------------------------------------------------------------------
	protected void UpdateTestTransform()
	{
		CameraManager manager = GetGame().GetCameraManager();
		if (!manager)
			return;

		CameraBase view = manager.CurrentCamera();
		if (!view)
			return;

		vector viewMat[4];
		view.GetWorldCameraTransform(viewMat);

		vector viewRot[3];
		viewRot[0] = viewMat[0];
		viewRot[1] = viewMat[1];
		viewRot[2] = viewMat[2];

		vector angles = Math3D.MatrixToAngles(viewRot);
		angles[0] = angles[0] + m_fAimYaw;
		angles[1] = m_fAimPitch;
		angles[2] = 0;

		vector aimRot[3];
		Math3D.AnglesToMatrix(angles, aimRot);

		m_mFeedTransform[0] = aimRot[0];
		m_mFeedTransform[1] = aimRot[1];
		m_mFeedTransform[2] = aimRot[2];
		m_mFeedTransform[3] = viewMat[3] + Vector(0, m_fTestHeight, 0);
		m_bHasTransform = true;
	}

	//------------------------------------------------------------------------------------------------
	// Settings the signal system drives. Kept here so it changes renderer state rather than reaching
	// into the widget directly.
	//------------------------------------------------------------------------------------------------
	void SetCamSlot(int slot)
	{
		if (slot < CAM_SLOT_MIN || slot > CAM_SLOT_MAX)
		{
			Print("[ACM RemoteFeed] Camera slot " + slot.ToString() + " is outside the usable range " + CAM_SLOT_MIN.ToString() + "-" + CAM_SLOT_MAX.ToString() + ". Ignored.", LogLevel.WARNING);
			return;
		}

		m_iCamSlot = slot;

		if (m_wRenderTarget)
			AttachRenderTarget();
	}

	//------------------------------------------------------------------------------------------------
	void SetMaxFPS(int fps)
	{
		m_iMaxFPS = fps;

		if (m_wRenderTarget && m_eState == ACM_ERemoteFeedState.RENDERING)
			m_wRenderTarget.SetMaxFPS(m_iMaxFPS);
	}

	//------------------------------------------------------------------------------------------------
	void SetReceiverGain(float gain)
	{
		m_Signal.SetReceiverGain(gain);
	}

	//------------------------------------------------------------------------------------------------
	void SetRenderScale(float scale)
	{
		m_fRenderScale = scale;

		if (m_wRenderTarget && m_fRenderScale > 0 && m_fRenderScale <= 1)
			m_wRenderTarget.SetResolutionScale(m_fRenderScale, m_fRenderScale);
	}

	//------------------------------------------------------------------------------------------------
	//------------------------------------------------------------------------------------------------
	// Told, not asked. The device knows where its own display sits; the renderer just needs numbers.
	//------------------------------------------------------------------------------------------------
	void SetDisplayRegion(ACM_RemoteFeedDisplayRegion region)
	{
		m_Region = region;

		if (m_wPanel && GetGame().GetWorkspace())
			ApplyGeometry(GetGame().GetWorkspace());
	}

	//------------------------------------------------------------------------------------------------
	void SetRegionSizeFraction(float fraction)
	{
		m_fRegionSizeFraction = fraction;

		if (m_wPanel && GetGame().GetWorkspace())
			ApplyGeometry(GetGame().GetWorkspace());
	}

	//------------------------------------------------------------------------------------------------
	// Inset from the region edge, as a fraction of it.
	//------------------------------------------------------------------------------------------------
	void SetRegionMargin(float fraction)
	{
		m_fRegionMargin = fraction;

		if (m_wPanel && GetGame().GetWorkspace())
			ApplyGeometry(GetGame().GetWorkspace());
	}

	//------------------------------------------------------------------------------------------------
	// Only used when docked to the screen rather than placed in a display region.



	//------------------------------------------------------------------------------------------------
	// Test camera aim. Yaw is an offset from the view direction; pitch is absolute.
	//------------------------------------------------------------------------------------------------
	void SetAim(float yawOffsetDegrees, float pitchDegrees)
	{
		m_fAimYaw = yawOffsetDegrees;
		m_fAimPitch = pitchDegrees;
	}

	//------------------------------------------------------------------------------------------------
	void SetTestHeight(float metres)
	{
		m_fTestHeight = metres;
	}
}
