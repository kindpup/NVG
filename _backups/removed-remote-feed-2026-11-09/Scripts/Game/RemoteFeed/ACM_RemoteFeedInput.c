//------------------------------------------------------------------------------------------------
// ACM_RemoteFeedInput.c
// Remote Feed keybinds (toggle / cycle source) for the local player. These used to live on the
// target detection device; with that system removed, any player can open the feed panel, and the
// feed sources' own access rules still decide what they may watch.
//------------------------------------------------------------------------------------------------
class ACM_RemoteFeedInput
{
	protected static bool s_bRegistered;

	// The same press can arrive more than once (character contexts and the menu context).
	protected static const int DEBOUNCE_MS = 250;
	protected static int s_iLastToggleTick;
	protected static int s_iLastCycleTick;

	// Panel defaults (what the detection device used to push).
	protected static const float RENDER_SCALE = 0.5;
	protected static const float RECEIVER_GAIN = 1.0;
	protected static const float SIZE_FRACTION = 0.42;
	protected static const float REGION_MARGIN = 0.04;

	//------------------------------------------------------------------------------------------------
	//! Safe to call as often as you like; attaches once a local controller and input manager exist.
	static void EnsureRegistered()
	{
		if (s_bRegistered || !GetGame())
			return;

		if (!GetGame().GetPlayerController())
			return;

		InputManager input = GetGame().GetInputManager();
		if (!input)
			return;

		input.AddActionListener("ACM_RemoteFeed_Toggle", EActionTrigger.DOWN, OnToggleInput);
		input.AddActionListener("ACM_RemoteFeed_Cycle", EActionTrigger.DOWN, OnCycleInput);
		s_bRegistered = true;
	}

	//------------------------------------------------------------------------------------------------
	protected static bool Debounced(inout int lastTick)
	{
		int now = System.GetTickCount();
		if (lastTick != 0 && now - lastTick < DEBOUNCE_MS)
			return true;

		lastTick = now;
		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected static void OnToggleInput()
	{
		if (!ACM_InputBindingHelper.ShouldHandleInput("ACM_RemoteFeed_Toggle"))
			return;

		if (Debounced(s_iLastToggleTick))
			return;

		ACM_RemoteFeedRenderer renderer = ACM_RemoteFeedRenderer.GetInstance();
		if (!renderer)
			return;

		PushDefaults(renderer);
		renderer.Toggle();
	}

	//------------------------------------------------------------------------------------------------
	protected static void OnCycleInput()
	{
		if (!ACM_InputBindingHelper.ShouldHandleInput("ACM_RemoteFeed_Cycle"))
			return;

		if (Debounced(s_iLastCycleTick))
			return;

		ACM_RemoteFeedRenderer renderer = ACM_RemoteFeedRenderer.GetInstance();
		if (!renderer || !renderer.IsActive())
			return;

		renderer.CycleSourceInput();
	}

	//------------------------------------------------------------------------------------------------
	//! Screen-docked panel in the top-left corner at normal size.
	protected static void PushDefaults(ACM_RemoteFeedRenderer renderer)
	{
		renderer.SetRenderScale(RENDER_SCALE);
		renderer.SetReceiverGain(RECEIVER_GAIN);
		renderer.SetDebugQuality(-1);
		renderer.SetRegionSizeFraction(SIZE_FRACTION);
		renderer.SetRegionMargin(REGION_MARGIN);
		renderer.SetSizeScale(1.0);
		renderer.SetCorner(ACM_ERemoteFeedCorner.TOP_LEFT);

		ACM_RemoteFeedDisplayRegion region = new ACM_RemoteFeedDisplayRegion();
		region.Set(ACM_ERemoteFeedRegionShape.FULLSCREEN, 0.5, 0.5, 0.5, 0.5);
		renderer.SetDisplayRegion(region);
	}
}
