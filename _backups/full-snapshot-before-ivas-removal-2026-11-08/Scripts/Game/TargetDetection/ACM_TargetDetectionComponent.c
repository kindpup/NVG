//------------------------------------------------------------------------------------------------
// ACM_TargetDetectionComponent.c
// Opt-in target detection / character highlighting for a single NVG device.
//
// Add this component to an NVG prefab that should have target detection. Devices without
// it behave exactly as before - the presence of this component IS the opt-in switch, and
// the system follows that device's own power state via ACM_NightVisionComponent.
//
// The component holds configuration and lifecycle only. All scanning and drawing lives in
// ACM_TargetDetectionSystem, which owns a single shared canvas so that two goggles cannot
// each spin up their own draw loop.
//------------------------------------------------------------------------------------------------

[ComponentEditorProps(category: "ACM/Equipment", description: "Adds NVG target detection (character highlighting) to this device")]
class ACM_TargetDetectionComponentClass : ScriptComponentClass
{
}

class ACM_TargetDetectionComponent : ScriptComponent
{
	//------------------------------------------------------------------------------------------------
	// Detection envelope
	//------------------------------------------------------------------------------------------------
	[Attribute("150", UIWidgets.Slider, desc: "Maximum detection range in metres. Note the practical ceiling is the server's network streaming distance - characters further out do not exist on this client at all, so raising this past ~500 buys nothing.", category: "ACM Target Detection - Envelope", params: "10 800 5")]
	protected float m_fMaxRange;

	[Attribute("55", UIWidgets.Slider, desc: "Detection cone in degrees, total (not half-angle). Targets outside the cone are ignored even in plain sight.", category: "ACM Target Detection - Envelope", params: "10 180 1")]
	protected float m_fFovDegrees;

	[Attribute("1", UIWidgets.CheckBox, desc: "Require an unobstructed line of sight. Turning this off makes the device see through walls.", category: "ACM Target Detection - Envelope")]
	protected bool m_bRequireLineOfSight;

	[Attribute("0.75", UIWidgets.Slider, desc: "Seconds a contact stays highlighted after line of sight breaks. Stops the outline strobing when a target passes behind light foliage. Raising this makes targets linger visibly after they take cover, so keep it short. Set to 0 for instant drop.", category: "ACM Target Detection - Envelope", params: "0 10 0.05")]
	protected float m_fMemorySeconds;

	[Attribute("24", UIWidgets.Slider, desc: "Hard cap on simultaneously highlighted contacts, nearest first. This is the main performance lever.", category: "ACM Target Detection - Envelope", params: "1 32 1")]
	protected int m_iMaxTargets;

	[Attribute("1", UIWidgets.CheckBox, desc: "Detect AI characters as well as players.", category: "ACM Target Detection - Envelope")]
	protected bool m_bDetectAI;

	[Attribute("1", UIWidgets.CheckBox, desc: "Detect dead and unconscious characters. Off means only characters that are still up get highlighted.", category: "ACM Target Detection - Envelope")]
	protected bool m_bDetectIncapacitated;

	//------------------------------------------------------------------------------------------------
	// IVAS contact behaviour
	//
	// These govern hostiles only. A hostile has to earn acquisition through sustained sight,
	// and the moment sight breaks its marker freezes at the last position the device actually
	// observed - it does not follow the target through the building. Friendlies and neutrals
	// are untouched by all of this and keep the original memory-window behaviour until the
	// IFF pass replaces it.
	//------------------------------------------------------------------------------------------------
	[Attribute("1", UIWidgets.CheckBox, desc: "Hostiles must earn acquisition through sustained line of sight, and their marker freezes at the last observed position when sight breaks. Turning this off reverts hostiles to the legacy memory window, which follows the target through cover - do not ship a device with this off.", category: "ACM Target Detection - IVAS")]
	protected bool m_bIvasHostileContacts;

	[Attribute("0.35", UIWidgets.Slider, desc: "Seconds of unbroken line of sight before a hostile becomes a confirmed contact. Below this it is a candidate and draws nothing. Raising it means a target crossing a doorway may never be acquired at all; lowering it toward zero makes acquisition instant, which reads as a wallhack that happens to blink.", category: "ACM Target Detection - IVAS", params: "0 3 0.05")]
	protected float m_fAcquireDwellSeconds;

	[Attribute("2", UIWidgets.Slider, desc: "Seconds a frozen contact holds full confidence after sight breaks, before it starts fading. The marker stays put throughout - this only controls when it begins to look stale.", category: "ACM Target Detection - IVAS", params: "0 30 0.5")]
	protected float m_fConfidenceHoldSeconds;

	[Attribute("10", UIWidgets.Slider, desc: "Seconds spent fading from full confidence to zero, measured after the hold window. Longer means stale contacts linger visibly.", category: "ACM Target Detection - IVAS", params: "0.5 60 0.5")]
	protected float m_fConfidenceDecaySeconds;

	[Attribute("12", UIWidgets.Slider, desc: "Seconds after last confirmed sight before a frozen contact is discarded entirely. Should be at least hold + decay, or contacts vanish while still visibly faded.", category: "ACM Target Detection - IVAS", params: "1 120 1")]
	protected float m_fContactExpireSeconds;

	[Attribute("0.5", UIWidgets.Slider, desc: "How much faster a distant contact goes stale. Both the confidence hold and the decay are multiplied by (1 - this * range/maxRange), so at 0.5 a contact seen at maximum range fades in half the time one at arm's length does. Rationale: a position report ages badly with distance - a target last seen 300m out has had time to be anywhere by the time you look back. Set to 0 for range-independent fading.", category: "ACM Target Detection - IVAS", params: "0 0.9 0.05")]
	protected float m_fRangeFadeScaling;

	[Attribute("0", UIWidgets.CheckBox, desc: "Draw a through-geometry outline on a contact while sight is actually confirmed. Off by default: it is the single biggest step this system can take toward reading as an ESP, and the box already tells the wearer where the contact is. When enabled it applies ONLY while the wearer currently has line of sight - a remembered contact, and in due course one shared over the data link, never receives one.", category: "ACM Target Detection - IVAS")]
	protected bool m_bIvasOutlineConfirmed;

	//------------------------------------------------------------------------------------------------
	// Friendly IFF
	//
	// A separate, deliberately cheap path. Friendlies get no raycasts, no acquisition dwell, no
	// confidence model and no frozen positions - they are drawn from their current position and
	// coloured by the tactical role their player has declared. Faction decides who is eligible;
	// role decides how they look.
	//------------------------------------------------------------------------------------------------
	[Attribute("3", UIWidgets.ComboBox, desc: "Which friendlies receive an IFF marker. This is a visibility decision only - role colouring is applied afterwards to whoever passes it. Defaults to all friendlies: the group modes depend on the wearer actually being in a group, and a device that shows nothing at all until you join one reads as broken rather than as configured.", category: "ACM Target Detection - Friendly IFF", enums: ParamEnumArray.FromEnum(ACM_TDFriendlyIffMode))]
	protected ACM_TDFriendlyIffMode m_eFriendlyIffMode;

	[Attribute("1", UIWidgets.ComboBox, desc: "How much each friendly marker says. Minimal is glyph and role; standard adds name and range; detailed adds the group. Raise it for a small element, lower it for a full platoon.", category: "ACM Target Detection - Friendly IFF", enums: ParamEnumArray.FromEnum(ACM_TDIffDetail))]
	protected ACM_TDIffDetail m_eIffDetail;

	[Attribute("300", UIWidgets.Slider, desc: "Friendlies beyond this range receive no marker. Independent of the hostile detection range - knowing where your squad is remains useful much further out than spotting an enemy does.", category: "ACM Target Detection - Friendly IFF", params: "10 1500 10")]
	protected float m_fFriendlyMaxRange;

	[Attribute("300", UIWidgets.Slider, desc: "Radius of the visor's squad roster. Every member of your own group inside this distance appears on the status strip whether or not you can see them, whether or not they are in front of you, and whatever the friendly marker mode is set to - the strip answers where your squad IS, not what is currently drawn.", category: "ACM Target Detection - Friendly IFF", params: "50 2000 10")]
	protected float m_fSquadRosterRange;

	[Attribute("24", UIWidgets.Slider, desc: "Most friendly markers drawn at once. When more are in range the lowest role priority is dropped first, so leaders and specialists survive a crowded display.", category: "ACM Target Detection - Friendly IFF", params: "1 64 1")]
	protected int m_iMaxFriendlies;

	[Attribute("1", UIWidgets.Slider, desc: "Seconds between rebuilds of the cached name, role, group and range text. Position still projects every frame - this only governs the strings, which change on the order of never.", category: "ACM Target Detection - Friendly IFF", params: "0.25 10 0.25")]
	protected float m_fFriendlyRefreshSeconds;

	[Attribute("6", UIWidgets.Slider, desc: "Half-size in pixels of the friendly role glyph. Fixed rather than derived from the target's on-screen height: a friendly marker is an IFF symbol, not an outline of a body.", category: "ACM Target Detection - Friendly IFF", params: "2 20 0.5")]
	protected float m_fFriendlyMarkerSize;

	[Attribute("1", UIWidgets.CheckBox, desc: "Outline friendlies in their role colour. Unlike the hostile outline this is NOT gated on line of sight - seeing your own squad through a wall is the entire point of IFF, and it costs no raycast because friendly position is known rather than detected.", category: "ACM Target Detection - Friendly IFF")]
	protected bool m_bOutlineFriendlies;

	[Attribute("120", UIWidgets.Slider, desc: "Friendlies past this range get a marker but no outline. Kept shorter than the marker range on purpose: a silhouette is for picking someone out of the immediate scene, and a whole platoon glowing across a valley is noise rather than information.", category: "ACM Target Detection - Friendly IFF", params: "0 800 10")]
	protected float m_fFriendlyOutlineRange;

	[Attribute("0.45", UIWidgets.Slider, desc: "Through-geometry strength of the friendly outline. Lower than a hostile's on purpose - a friendly behind cover should register at the edge of attention, not compete with the target you are actually looking at.", category: "ACM Target Detection - Friendly IFF", params: "0 1 0.05")]
	protected float m_fFriendlyOutlineThroughWall;

	[Attribute("0.12", UIWidgets.Slider, desc: "Fill strength of the friendly outline where the body is directly visible. Keep it low: this is a silhouette edge, not a coloured-in figure.", category: "ACM Target Detection - Friendly IFF", params: "0 1 0.02")]
	protected float m_fFriendlyOutlineFill;

	// SCOPED MESH HIGHLIGHT
	//
	// Used only while looking through a magnified optic, where the engine outliner cannot follow -
	// OutlineEntity is a main-view pass and the scope glass is a different camera's image. This is
	// a material change instead, so it renders wherever the model renders.
	//
	// A slider rather than a constant because the emissive scale is not documented anywhere I can
	// find, so the only way to land on a good number is to look through the scope and turn the dial.
	[Attribute("24", UIWidgets.Slider, desc: "Emissive strength of the scoped mesh highlight. The engine outliner cannot draw into a PIP scope, so a magnified contact is lit by tinting its materials instead. Turn it up until a contact reads clearly against the background at the ranges you actually shoot at; too high and the figure blows out into a flat silhouette with no detail left to identify.", category: "ACM Target Detection - Scoped Highlight", params: "0 128 1")]
	protected float m_fScopedHighlightEmissive;
	[Attribute("2.05", UIWidgets.Slider, desc: "Height in metres above the friendly's feet at which the marker floats, so it sits clear of the head rather than on the body.", category: "ACM Target Detection - Friendly IFF", params: "0 4 0.05")]
	protected float m_fFriendlyMarkerRise;

	//------------------------------------------------------------------------------------------------
	// Tactical data link
	//
	// Sharing and receiving are separate switches on purpose. A device may be a good sensor and a
	// poor terminal, or the reverse, and a mission may want spotters who feed the net without
	// carrying the net's picture themselves.
	//------------------------------------------------------------------------------------------------
	[Attribute("1", UIWidgets.CheckBox, desc: "Report hostiles this device confirms by line of sight to the tactical data link. The server validates every report regardless of this setting - turning it off simply stops the device offering them.", category: "ACM Target Detection - Data Link")]
	protected bool m_bShareContacts;

	[Attribute("1", UIWidgets.CheckBox, desc: "Display contacts shared by friendly sensors. Received contacts always draw from their stored position and can never receive an entity outline, whatever sensor originally reported them.", category: "ACM Target Detection - Data Link")]
	protected bool m_bReceiveContacts;

	[Attribute("2", UIWidgets.Slider, desc: "Minimum seconds between reports about the SAME target from this device. A client-side courtesy that keeps ordinary traffic sane; the server enforces its own budget against clients that ignore it.", category: "ACM Target Detection - Data Link", params: "0.25 30 0.25")]
	protected float m_fReportIntervalSeconds;

	//------------------------------------------------------------------------------------------------
	// Display region
	//
	// Markers outside the device's usable display are not drawn. This is a display property, not a
	// sensor one: the system still knows about those contacts, it simply has nowhere to put them.
	//------------------------------------------------------------------------------------------------
	[Attribute("1", UIWidgets.ComboBox, desc: "Shape of the usable display region. Circle suits tube optics, rectangle a flat visor, none draws across the whole screen.", category: "ACM Target Detection - Display", enums: ParamEnumArray.FromEnum(ACM_TDDisplayMask))]
	protected ACM_TDDisplayMask m_eDisplayMask;

	[Attribute("0.5", UIWidgets.Slider, desc: "Circle radius as a fraction of SCREEN HEIGHT. 0.5 is a circle touching the top and bottom of the screen, which only trims the corners; tighten it to match the actual tube.", category: "ACM Target Detection - Display", params: "0.05 1 0.01")]
	protected float m_fMaskRadius;

	[Attribute("0.42", UIWidgets.Slider, desc: "Rectangle half-width as a fraction of screen width. Ignored unless the mask is set to rectangle.", category: "ACM Target Detection - Display", params: "0.05 0.5 0.01")]
	protected float m_fMaskHalfWidth;

	[Attribute("0.42", UIWidgets.Slider, desc: "Rectangle half-height as a fraction of screen height. Ignored unless the mask is set to rectangle.", category: "ACM Target Detection - Display", params: "0.05 0.5 0.01")]
	protected float m_fMaskHalfHeight;

	[Attribute("0.5", UIWidgets.Slider, desc: "Horizontal centre of the display region, as a fraction of screen width. 0.5 is centred.", category: "ACM Target Detection - Display", params: "0 1 0.01")]
	protected float m_fMaskCenterX;

	[Attribute("0.5", UIWidgets.Slider, desc: "Vertical centre of the display region, as a fraction of screen height. 0.5 is centred.", category: "ACM Target Detection - Display", params: "0 1 0.01")]
	protected float m_fMaskCenterY;

	[Attribute("0.12", UIWidgets.Slider, desc: "Fraction of the region's extent over which markers fade out as they approach its edge. Without some feather a contact walking off the edge of the display vanishes in one frame, which reads as a glitch. Set to 0 for a hard edge.", category: "ACM Target Detection - Display", params: "0 0.5 0.01")]
	protected float m_fMaskFeather;

	//------------------------------------------------------------------------------------------------
	// Remote feed
	//
	// The feed panel's position and size live in UI/layouts/HUD/ACM_RemoteFeed.layout, not here -
	// edit them in the Workbench UI editor where you can see what you are doing. These are only
	// the things a layout cannot express.
	//------------------------------------------------------------------------------------------------
	[Attribute("0", UIWidgets.CheckBox, desc: "Override the layout and place the feed inside this device's display region instead. Only sensible on a rectangular visor: inside a circular mask the usable area is a small centred square, so the feed ends up near the middle of the screen. Off means the layout decides.", category: "ACM Target Detection - Remote Feed")]
	protected bool m_bFeedInDisplayRegion;

	[Attribute("0.42", UIWidgets.Slider, desc: "Feed width as a fraction of the display region. Used only when the override above is on.", category: "ACM Target Detection - Remote Feed", params: "0.1 1 0.01")]
	protected float m_fFeedSizeFraction;

	[Attribute("0.04", UIWidgets.Slider, desc: "Inset from the edge of the display region, as a fraction of it. Used only when the override above is on.", category: "ACM Target Detection - Remote Feed", params: "0 0.4 0.01")]
	protected float m_fFeedRegionMargin;

	[Attribute("0.5", UIWidgets.Slider, desc: "Render scale of the feed. The render target's resolution is the panel's size times this, so 0.5 is a quarter of the pixels. This is the performance knob - resize the panel in the layout to change how big it looks.", category: "ACM Target Detection - Remote Feed", params: "0.1 1 0.05")]
	protected float m_fFeedRenderScale;

	[Attribute("1.0", UIWidgets.Slider, desc: "This device's RECEIVING antenna. Scales every remote feed it displays, so a cheap set sees every source worse and a command variant sees them all better. The far end contributes its own gain from the source profile - the link has two ends.", category: "ACM Target Detection - Remote Feed", params: "0.25 1.5 0.05")]
	protected float m_fFeedReceiverGain;

	[Attribute("-1", UIWidgets.Slider, desc: "TESTING ONLY. Forces the link to a fixed quality so the degradation can be seen without staging the geometry for it. -1 is off. Try 0.3 for heavy static and coarse pixels, 0.7 for mild interference, 0.02 to watch it fall to SIGNAL LOST and recover.", category: "ACM Target Detection - Remote Feed", params: "-1 1 0.01")]
	protected float m_fFeedDebugQuality;

	//------------------------------------------------------------------------------------------------
	// Performance
	//------------------------------------------------------------------------------------------------
	[Attribute("100", UIWidgets.Slider, desc: "Milliseconds between full rescans. Drawing still happens every frame, so lowering this does not make the outlines smoother - it only makes the system react to new contacts faster, at real CPU cost.", category: "ACM Target Detection - Performance", params: "50 1000 10")]
	protected int m_iScanIntervalMs;

	[Attribute("10", UIWidgets.Slider, desc: "Maximum line-of-sight raycasts per scan. Contacts beyond this budget are checked on following scans, round robin, so a crowded area degrades in latency rather than in frame rate. Lowering this makes targets slower to disappear when they break line of sight.", category: "ACM Target Detection - Performance", params: "1 32 1")]
	protected int m_iTracesPerScan;

	[Attribute("0", UIWidgets.ComboBox, desc: "How candidate characters are gathered. Sphere query scales with detection range; agent scan walks every AI in the world and exists only as a fallback.", category: "ACM Target Detection - Performance", enums: ParamEnumArray.FromEnum(ACM_TDScanMode))]
	protected ACM_TDScanMode m_eScanMode;

	//------------------------------------------------------------------------------------------------
	// Appearance
	//------------------------------------------------------------------------------------------------
	[Attribute("2", UIWidgets.Slider, desc: "Box stroke width in pixels.", category: "ACM Target Detection - Appearance", params: "1 6 0.5")]
	protected float m_fLineWidth;

	[Attribute("1", UIWidgets.Slider, desc: "Overall marker size multiplier. The box is sized from the target's on-screen height; this scales it about its own centre, so 1 frames the body and lower values draw a tighter mark that stays clear of it.", category: "ACM Target Detection - Appearance", params: "0.25 2 0.05")]
	protected float m_fMarkerScale;

	[Attribute("0.22", UIWidgets.Slider, desc: "Marker half-width as a fraction of its on-screen height. Raise it for a wider, squarer box; lower it for a narrow one. Proportions hold at every range because both axes derive from the same measured height.", category: "ACM Target Detection - Appearance", params: "0.08 0.6 0.01")]
	protected float m_fBoxAspect;

	[Attribute("4", UIWidgets.Slider, desc: "Smallest on-screen marker height, in pixels, still worth drawing. Below this a contact is a couple of pixels of noise, so it is skipped entirely.", category: "ACM Target Detection - Appearance", params: "2 40 1")]
	protected float m_fMinMarkerHeight;

	[Attribute("0.28", UIWidgets.Slider, desc: "Corner-bracket arm length as a fraction of each box side. At 0.5 the brackets meet and the marker becomes a closed rectangle; lower values leave more of each side open.", category: "ACM Target Detection - Appearance", params: "0.1 0.5 0.01")]
	protected float m_fCornerFraction;

	[Attribute("6", UIWidgets.Slider, desc: "Gap in pixels between the top of the marker and the label above it.", category: "ACM Target Detection - Appearance", params: "0 40 1")]
	protected float m_fLabelOffset;

	[Attribute("1", UIWidgets.CheckBox, desc: "Draw a text label above each contact.", category: "ACM Target Detection - Appearance")]
	protected bool m_bDrawLabel;

	[Attribute("1", UIWidgets.CheckBox, desc: "Include the range to the contact in its label.", category: "ACM Target Detection - Appearance")]
	protected bool m_bLabelShowDistance;

	[Attribute("0", UIWidgets.CheckBox, desc: "Include the player name in the label. Off by default - a name tag through the goggles is a much bigger gameplay change than a shape is.", category: "ACM Target Detection - Appearance")]
	protected bool m_bLabelShowName;

	[Attribute("120", UIWidgets.Slider, desc: "Labels are hidden past this range even when the outline is still drawn, so distant contacts do not turn into a wall of text.", category: "ACM Target Detection - Appearance", params: "10 500 5")]
	protected float m_fLabelMaxRange;

	//------------------------------------------------------------------------------------------------
	// IFF colours
	//
	// These are drawn by the UI compositor, which runs after the NVG post-process, so the
	// phosphor material does NOT tint them. Pick colours that read against a green or
	// white tinted scene rather than expecting the tube colour to blend them in.
	//------------------------------------------------------------------------------------------------
	[Attribute("CIV", UIWidgets.EditBox, desc: "Comma-separated faction keys that are friendly at the engine level but must never receive friendly IFF. Reforger reports civilians as friendly to everyone, which would otherwise give villagers a full IFF marker and an inferred tactical role. Your own faction is always friendly regardless of this list.", category: "ACM Target Detection - IFF")]
	protected string m_sNonFriendlyFactionKeys;

	[Attribute("1", UIWidgets.CheckBox, desc: "Colour contacts by faction. Off means every contact uses the unknown colour, which is the honest behaviour for a device that only detects a human shape.", category: "ACM Target Detection - IFF")]
	protected bool m_bUseFactionColors;

	[Attribute("1", UIWidgets.CheckBox, desc: "Use the perceived faction rather than the true one, so a character in enemy uniform reads as that faction. Recommended - an optical device cannot see through a disguise either.", category: "ACM Target Detection - IFF")]
	protected bool m_bUsePerceivedFaction;

	[Attribute("0.35 0.85 0.95 0.85", UIWidgets.ColorPicker, desc: "Own faction and allies.", category: "ACM Target Detection - IFF")]
	protected ref Color m_ColFriendly;

	[Attribute("0.95 0.45 0.35 0.85", UIWidgets.ColorPicker, desc: "Hostile faction.", category: "ACM Target Detection - IFF")]
	protected ref Color m_ColHostile;

	[Attribute("0.95 0.85 0.45 0.85", UIWidgets.ColorPicker, desc: "Factions that are neither friendly nor hostile.", category: "ACM Target Detection - IFF")]
	protected ref Color m_ColNeutral;

	[Attribute("0.90 0.86 0.72 0.85", UIWidgets.ColorPicker, desc: "No resolvable faction, and the colour used for everything when faction colouring is off.", category: "ACM Target Detection - IFF")]
	protected ref Color m_ColUnknown;

	//------------------------------------------------------------------------------------------------
	//------------------------------------------------------------------------------------------------
	// Visor presentation
	//
	// The frame the wearer looks through, and what it displaces. Local to this screen: none of it
	// replicates and none of it is visible to anyone else.
	//------------------------------------------------------------------------------------------------
	[Attribute("1", UIWidgets.CheckBox, desc: "Draw the IVAS visor frame - corner brackets, side ticks and the ID / mode / contact readouts - while the visor is down.", category: "ACM Target Detection - Visor Display")]
	protected bool m_bVisorOverlay;

	[Attribute("1", UIWidgets.CheckBox, desc: "Hide the ordinary game HUD while the visor is down, so the visor frame is what the wearer sees. Everything hidden is restored exactly as it was when the visor comes up - anything the game had already hidden for its own reasons stays hidden.", category: "ACM Target Detection - Visor Display")]
	protected bool m_bSuppressVanillaHud;

	[Attribute("1", UIWidgets.CheckBox, desc: "Hide chat panels along with the rest of the HUD. Separate from the list below because chat is not an info display and has its own show/hide path.", category: "ACM Target Detection - Visor Display")]
	protected bool m_bSuppressChat;

	[Attribute("WeaponInfo,WeaponSwitching,NotificationsLog,AvailableActions,ChatHud,HintInfo,NameTag", UIWidgets.EditBox, desc: "Comma-separated fragments matched against each HUD display's CLASS NAME and its LAYOUT PATH, case-insensitive - a display is hidden if either contains any fragment. Layout path matters because a live HUD reports several different displays as bare SCR_InfoDisplayExtended, and only the path tells them apart. Defaults are the six confirmed against a running HUD, plus NameTag - the visor draws its own squad strip and contact labels, so leaving the vanilla nametags up shows every player's name twice. NameTag matches SCR_NameTagDisplay by class name. Deliberately a block list rather than hiding everything - widening it is one edit, whereas hiding the respawn or map UI by accident is a player who cannot get back into the game.", category: "ACM Target Detection - Visor Display")]
	protected string m_sSuppressedHudClasses;

	//------------------------------------------------------------------------------------------------
	// Non-combatants
	//
	// Civilians, characters with no resolvable faction, and neutral third parties. Observed, never
	// tracked: they earn a marker by being seen and nothing else. None of this touches the hostile
	// pipeline - the fork happens before a contact object exists.
	//------------------------------------------------------------------------------------------------
	[Attribute("1", UIWidgets.CheckBox, desc: "Mark civilians. Presentation only - with this off a civilian is still classified as a civilian and still cannot enter hostile acquisition, the marker simply is not drawn.", category: "ACM Target Detection - Non-Combatants")]
	protected bool m_bCivilianIff;

	[Attribute("1", UIWidgets.CheckBox, desc: "Mark characters whose faction the device could not resolve. Off means they are absent from the display; they are never reclassified as anything else.", category: "ACM Target Detection - Non-Combatants")]
	protected bool m_bUnknownContacts;

	[Attribute("150", UIWidgets.Slider, desc: "Non-combatants beyond this range get no marker. Kept well short of the hostile envelope on purpose: identifying the man on the corner is useful, identifying a figure at 600m is a dot and a wasted raycast.", category: "ACM Target Detection - Non-Combatants", params: "10 600 5")]
	protected float m_fCivilianMaxRange;

	[Attribute("12", UIWidgets.Slider, desc: "Most non-combatant markers at once, nearest first.", category: "ACM Target Detection - Non-Combatants", params: "1 40 1")]
	protected int m_iMaxNonCombatants;

	[Attribute("4", UIWidgets.Slider, desc: "Line-of-sight raycasts per scan spent on non-combatants. Separate from the hostile budget so a crowded market cannot starve target acquisition.", category: "ACM Target Detection - Non-Combatants", params: "1 16 1")]
	protected int m_iNonCombatantTracesPerScan;

	[Attribute("1.0", UIWidgets.Slider, desc: "Seconds a non-combatant marker survives after sight is lost. It stays FROZEN at the last observed position for this window and fades out - it does not follow the character. Keep it short; this only exists so a marker does not strobe behind passing traffic.", category: "ACM Target Detection - Non-Combatants", params: "0 5 0.1")]
	protected float m_fNonCombatantMemorySeconds;

	[Attribute("1.95", UIWidgets.Slider, desc: "Height in metres above the ground at which a non-combatant marker floats.", category: "ACM Target Detection - Non-Combatants", params: "0 4 0.05")]
	protected float m_fNonCombatantMarkerRise;

	[Attribute("5", UIWidgets.Slider, desc: "Half-size in pixels of the non-combatant glyph. Smaller than the friendly glyph on purpose - this is information, not a call to action.", category: "ACM Target Detection - Non-Combatants", params: "2 20 0.5")]
	protected float m_fNonCombatantMarkerSize;

	[Attribute("0.95 0.72 0.25 0.80", UIWidgets.ColorPicker, desc: "Civilians. Deliberately not the neutral colour - the two now mean different things.", category: "ACM Target Detection - Non-Combatants")]
	protected ref Color m_ColCivilian;

	//------------------------------------------------------------------------------------------------
	// Presentation priority and clutter budgets
	//
	// These decide what the wearer SEES. Nothing here may remove a contact from the store, clear a
	// network id or reset a confidence value - a busy screen is not a statement about what the
	// squad knows.
	//------------------------------------------------------------------------------------------------
	[Attribute("50", UIWidgets.Slider, desc: "Metres. A hostile in confirmed sight inside this range is CRITICAL and is exempt from every display budget. This is the setting that stops a nearby threat vanishing because a drone just shared twenty contacts.", category: "ACM Target Detection - Presentation", params: "5 300 5")]
	protected float m_fCriticalRangeMetres;

	[Attribute("0.40", UIWidgets.Slider, desc: "Confidence at or above which a frozen contact is still presented at NORMAL. Below it the contact drops to LOW.", category: "ACM Target Detection - Presentation", params: "0.05 0.95 0.05")]
	protected float m_fNormalConfidenceThreshold;

	[Attribute("0.15", UIWidgets.Slider, desc: "Confidence below which a contact is STALE - a mark that something was here, and no label.", category: "ACM Target Detection - Presentation", params: "0.01 0.5 0.01")]
	protected float m_fLowConfidenceThreshold;

	[Attribute("8", UIWidgets.Slider, desc: "Seconds since last confirmed sight after which a contact is STALE regardless of confidence. Catches shared contacts, which carry no local confidence curve.", category: "ACM Target Detection - Presentation", params: "1 60 1")]
	protected float m_fStaleAgeSeconds;

	[Attribute("10", UIWidgets.Slider, desc: "How many hostiles may carry a label at once. CRITICAL contacts are exempt. Past this, contacts are presented one tier down rather than removed.", category: "ACM Target Detection - Presentation", params: "1 32 1")]
	protected int m_iFullDetailBudget;

	[Attribute("24", UIWidgets.Slider, desc: "How many hostiles may carry any marker at all. CRITICAL contacts are exempt. Contacts past this draw nothing and are otherwise completely untouched - they keep ageing, keep deduping and keep their network identity.", category: "ACM Target Detection - Presentation", params: "1 48 1")]
	protected int m_iVisibleContactBudget;

	[Attribute("1", UIWidgets.ComboBox, desc: "Which classifications reach the display. Friendly IFF is independent of this and is never switched off by it.", category: "ACM Target Detection - Presentation", enums: ParamEnumArray.FromEnum(ACM_TDContactFilter))]
	protected ACM_TDContactFilter m_eContactFilter;

	[Attribute("1", UIWidgets.ComboBox, desc: "How much a hostile marker says. Independent of the filter and of the budgets.", category: "ACM Target Detection - Presentation", enums: ParamEnumArray.FromEnum(ACM_TDContactDetail))]
	protected ACM_TDContactDetail m_eContactDetail;

	[Attribute("1", UIWidgets.CheckBox, desc: "Draw STALE contacts as a small mark. Off hides them; the contacts themselves are unaffected.", category: "ACM Target Detection - Presentation")]
	protected bool m_bShowStaleContacts;

	[Attribute("1", UIWidgets.ComboBox, desc: "Default marker size preset. The radial cycles this at runtime; discrete presets rather than a slider because it has to be usable on a gamepad.", category: "ACM Target Detection - Presentation", enums: ParamEnumArray.FromEnum(ACM_TDMarkerScalePreset))]
	protected ACM_TDMarkerScalePreset m_eMarkerScalePreset;

	//------------------------------------------------------------------------------------------------
	// Master switches
	//
	// Defaults for the runtime state the radial menu drives. The attribute is what the device ships
	// with; the runtime mirror below is what the wearer chose. Nothing here replicates.
	//------------------------------------------------------------------------------------------------
	[Attribute("1", UIWidgets.CheckBox, desc: "IVAS augmented presentation starts on. Turning it off at runtime hides every marker, suspends data link presentation and drops the remote feed - but leaves night vision and thermal alone, because those are separate hardware.", category: "ACM Target Detection - Master")]
	protected bool m_bIvasEnabledDefault;

	[Attribute("1", UIWidgets.CheckBox, desc: "Marker overlay starts on. A narrower switch than the IVAS master: this hides the markers and leaves the remote feed up.", category: "ACM Target Detection - Master")]
	protected bool m_bHudEnabledDefault;

	[Attribute("1", UIWidgets.CheckBox, desc: "Friendly IFF starts on. The scope setting decides which friendlies; this decides whether any are drawn.", category: "ACM Target Detection - Master")]
	protected bool m_bFriendlyIffEnabledDefault;

	[Attribute("1", UIWidgets.CheckBox, desc: "Hostile acquisition starts on. Off means no hostile is acquired or drawn; classification still runs, and shared contacts are still received and stored.", category: "ACM Target Detection - Master")]
	protected bool m_bHostileTrackingDefault;

	[Attribute("1", UIWidgets.CheckBox, desc: "Data link starts on. Parent gate over receive and share - switching it off suspends both and switching it back on restores whatever the wearer had each of them set to.", category: "ACM Target Detection - Master")]
	protected bool m_bDataLinkEnabledDefault;

	[Attribute("1", UIWidgets.ComboBox, desc: "Default remote feed size preset.", category: "ACM Target Detection - Master", enums: ParamEnumArray.FromEnum(ACM_TDFeedSizePreset))]
	protected ACM_TDFeedSizePreset m_eFeedSizePreset;

	[Attribute("0", UIWidgets.ComboBox, desc: "Default remote feed corner.", category: "ACM Target Detection - Master", enums: ParamEnumArray.FromEnum(ACM_TDFeedPositionPreset))]
	protected ACM_TDFeedPositionPreset m_eFeedPositionPreset;

	//------------------------------------------------------------------------------------------------
	// RUNTIME PREFERENCES
	//
	// The wearer's choices, seeded from the attributes above at post-init. Every one of these is
	// LOCAL - none is replicated, none writes an MP signal, and none can change anything on another
	// player's screen. That is not incidental: the last multiplayer defect in this system was a
	// presentation-shaped switch that turned out to write a replicated signal for every client.
	//
	// The menu reads these through the accessors and writes them through the setters. It stores
	// nothing itself, so closing and reopening the radial cannot lose a setting.
	//------------------------------------------------------------------------------------------------
	protected bool m_bRtIvas = true;
	protected bool m_bRtHud = true;
	protected bool m_bRtFriendlyIff = true;
	protected bool m_bRtCivilianIff = true;
	protected bool m_bRtUnknown = true;
	protected bool m_bRtHostileTracking = true;
	protected bool m_bRtOutlineFriendlies = true;
	protected bool m_bRtOutlineHostiles;
	protected bool m_bRtShowStale = true;
	protected bool m_bRtDrawLabel = true;
	protected bool m_bRtLabelDistance = true;
	protected bool m_bRtLabelName;

	// Master gate and the two intents it gates. Kept as three values so the master can be switched
	// off and back on without overwriting what the wearer had chosen underneath it.
	protected bool m_bRtDataLink = true;
	protected bool m_bRtReceiveIntent = true;
	protected bool m_bRtShareIntent = true;

	protected ACM_TDFriendlyIffMode m_eRtFriendlyIffMode;
	protected ACM_TDIffDetail m_eRtIffDetail;
	protected ACM_TDContactFilter m_eRtContactFilter;
	protected ACM_TDContactDetail m_eRtContactDetail;
	protected ACM_TDMarkerScalePreset m_eRtMarkerScale;
	protected ACM_TDFeedSizePreset m_eRtFeedSize;
	protected ACM_TDFeedPositionPreset m_eRtFeedPosition;

	// What the feed was doing when the IVAS master went off, so switching back on restores it.
	protected bool m_bFeedWasActiveIvas;

	protected bool m_bActive;

	//------------------------------------------------------------------------------------------------
	// Accessors used by the system. Kept explicit rather than making the fields public so
	// the attribute block stays the single source of truth.
	//------------------------------------------------------------------------------------------------
	float GetMaxRange() { return m_fMaxRange; }
	float GetFovDegrees() { return m_fFovDegrees; }
	bool RequiresLineOfSight() { return m_bRequireLineOfSight; }
	float GetMemorySeconds() { return m_fMemorySeconds; }
	int GetMaxTargets() { return m_iMaxTargets; }
	bool DetectsAI() { return m_bDetectAI; }
	bool DetectsIncapacitated() { return m_bDetectIncapacitated; }

	bool UsesIvasHostileContacts() { return m_bIvasHostileContacts; }
	float GetAcquireDwellSeconds() { return m_fAcquireDwellSeconds; }
	float GetConfidenceHoldSeconds() { return m_fConfidenceHoldSeconds; }
	float GetConfidenceDecaySeconds() { return m_fConfidenceDecaySeconds; }
	float GetContactExpireSeconds() { return m_fContactExpireSeconds; }
	bool OutlinesConfirmedContacts() { return m_bRtOutlineHostiles && IsHostileTrackingEnabled() && ACM_IffServerSettings.Allows(ACM_IffServerSettings.HOSTILE_OUTLINE); }
	float GetRangeFadeScaling() { return m_fRangeFadeScaling; }

	int GetScanIntervalMs() { return m_iScanIntervalMs; }
	int GetTracesPerScan() { return m_iTracesPerScan; }
	ACM_TDScanMode GetScanMode() { return m_eScanMode; }

	float GetLineWidth() { return m_fLineWidth; }
	// The preset multiplies what the prefab authored rather than replacing it, so a device tuned
	// for a tight mark still draws a tight mark at LARGE.
	float GetMarkerScale()
	{
		float factor = 1.0;

		if (m_eRtMarkerScale == ACM_TDMarkerScalePreset.SMALL)
			factor = 0.72;
		else if (m_eRtMarkerScale == ACM_TDMarkerScalePreset.LARGE)
			factor = 1.40;

		return m_fMarkerScale * factor;
	}
	float GetBoxAspect() { return m_fBoxAspect; }
	float GetMinMarkerHeight() { return m_fMinMarkerHeight; }
	float GetCornerFraction() { return m_fCornerFraction; }
	float GetLabelOffset() { return m_fLabelOffset; }

	// The master gate collapses to OFF rather than being a second test everywhere the mode is read.
	// One place to get wrong instead of five.
	ACM_TDFriendlyIffMode GetFriendlyIffMode()
	{
		if (!IsFriendlyIffEnabled())
			return ACM_TDFriendlyIffMode.OFF;

		return m_eRtFriendlyIffMode;
	}

	ACM_TDIffDetail GetIffDetail() { return m_eRtIffDetail; }
	float GetFriendlyMaxRange() { return m_fFriendlyMaxRange; }
	float GetSquadRosterRange() { return m_fSquadRosterRange; }
	int GetMaxFriendlies() { return m_iMaxFriendlies; }
	float GetFriendlyRefreshSeconds() { return m_fFriendlyRefreshSeconds; }
	float GetFriendlyMarkerSize() { return m_fFriendlyMarkerSize; }
	float GetFriendlyMarkerRise() { return m_fFriendlyMarkerRise; }
	bool OutlinesFriendlies() { return m_bRtOutlineFriendlies && IsFriendlyIffEnabled() && ACM_IffServerSettings.Allows(ACM_IffServerSettings.FRIENDLY_OUTLINE); }
	float GetFriendlyOutlineRange() { return m_fFriendlyOutlineRange; }
	float GetFriendlyOutlineThroughWall() { return m_fFriendlyOutlineThroughWall; }
	float GetFriendlyOutlineFill() { return m_fFriendlyOutlineFill; }
	int GetScopedHighlightEmissive() { return Math.Round(m_fScopedHighlightEmissive); }

	// Effective, not intent. The master gate suspends both without touching what the wearer had
	// each of them set to, which is what makes turning the link off and on again a no-op rather
	// than a reset.
	bool SharesContacts() { return IsDataLinkEnabled() && m_bRtShareIntent && ACM_IffServerSettings.Allows(ACM_IffServerSettings.HOSTILE); }
	bool ReceivesContacts() { return IsDataLinkEnabled() && m_bRtReceiveIntent && ACM_IffServerSettings.Allows(ACM_IffServerSettings.HOSTILE); }

	// Intent, for the menu. These are what the entries display and cycle.
	bool IsDataLinkEnabled() { return m_bRtDataLink && ACM_IffServerSettings.Allows(ACM_IffServerSettings.DATA_LINK); }
	bool IsShareIntent() { return m_bRtShareIntent; }
	bool IsReceiveIntent() { return m_bRtReceiveIntent; }
	float GetReportIntervalSeconds() { return m_fReportIntervalSeconds; }

	ACM_TDDisplayMask GetDisplayMask() { return m_eDisplayMask; }
	float GetMaskRadius() { return m_fMaskRadius; }
	float GetMaskHalfWidth() { return m_fMaskHalfWidth; }
	float GetMaskHalfHeight() { return m_fMaskHalfHeight; }
	float GetMaskCenterX() { return m_fMaskCenterX; }
	float GetMaskCenterY() { return m_fMaskCenterY; }
	float GetMaskFeather() { return m_fMaskFeather; }
	bool DrawsLabel() { return m_bRtDrawLabel; }
	bool LabelShowsDistance() { return m_bRtLabelDistance; }
	bool LabelShowsName() { return m_bRtLabelName; }
	float GetLabelMaxRange() { return m_fLabelMaxRange; }

	bool UsesFactionColors() { return m_bUseFactionColors; }
	string GetNonFriendlyFactionKeys() { return m_sNonFriendlyFactionKeys; }

	// The same authored list, read by the classifier.
	//
	// The field keeps its old name deliberately. Renaming it would be honest and would also silently
	// reset every prefab that has an authored value, because the attribute is matched by name.
	string GetCivilianFactionKeys() { return m_sNonFriendlyFactionKeys; }
	bool UsesPerceivedFaction() { return m_bUsePerceivedFaction; }

	//------------------------------------------------------------------------------------------------
	// Non-combatant and presentation accessors.
	//------------------------------------------------------------------------------------------------
	bool IsCivilianIffEnabled() { return m_bRtCivilianIff && ACM_IffServerSettings.Allows(ACM_IffServerSettings.CIVILIAN); }
	bool ShowsUnknownContacts() { return m_bRtUnknown && ACM_IffServerSettings.Allows(ACM_IffServerSettings.UNKNOWN); }
	float GetCivilianMaxRange() { return m_fCivilianMaxRange; }
	int GetMaxNonCombatants() { return m_iMaxNonCombatants; }
	int GetNonCombatantTracesPerScan() { return m_iNonCombatantTracesPerScan; }
	float GetNonCombatantMemorySeconds() { return m_fNonCombatantMemorySeconds; }
	float GetNonCombatantMarkerRise() { return m_fNonCombatantMarkerRise; }
	float GetNonCombatantMarkerSize() { return m_fNonCombatantMarkerSize; }

	float GetCriticalRangeMetres() { return m_fCriticalRangeMetres; }
	float GetNormalConfidenceThreshold() { return m_fNormalConfidenceThreshold; }
	float GetLowConfidenceThreshold() { return m_fLowConfidenceThreshold; }
	float GetStaleAgeSeconds() { return m_fStaleAgeSeconds; }
	int GetFullDetailBudget() { return m_iFullDetailBudget; }
	int GetVisibleContactBudget() { return m_iVisibleContactBudget; }
	ACM_TDContactFilter GetContactFilter() { return m_eRtContactFilter; }
	ACM_TDContactDetail GetContactDetail() { return m_eRtContactDetail; }
	bool ShowsStaleContacts() { return m_bRtShowStale; }
	ACM_TDMarkerScalePreset GetMarkerScalePreset() { return m_eRtMarkerScale; }

	bool DrawsVisorOverlay() { return m_bVisorOverlay; }
	bool SuppressesVanillaHud() { return m_bSuppressVanillaHud; }
	bool SuppressesChat() { return m_bSuppressChat; }
	string GetSuppressedHudClasses() { return m_sSuppressedHudClasses; }

	bool IsIvasEnabled() { return m_bRtIvas; }
	bool IsHudEnabled() { return m_bRtHud; }
	bool IsFriendlyIffEnabled() { return m_bRtFriendlyIff && ACM_IffServerSettings.Allows(ACM_IffServerSettings.FRIENDLY); }
	bool IsHostileTrackingEnabled() { return m_bRtHostileTracking && ACM_IffServerSettings.Allows(ACM_IffServerSettings.HOSTILE); }

	// Raw scope, without the master gate. The menu shows the wearer what the scope is set to even
	// while friendly IFF is off, because "GROUP, currently hidden" is information and "OFF" is not.
	ACM_TDFriendlyIffMode GetFriendlyIffScope() { return m_eRtFriendlyIffMode; }

	ACM_TDFeedSizePreset GetFeedSizePreset() { return m_eRtFeedSize; }
	ACM_TDFeedPositionPreset GetFeedPositionPreset() { return m_eRtFeedPosition; }

	//------------------------------------------------------------------------------------------------
	// SETTERS
	//
	// The whole write surface for the radial menu. Every one of these is local to this machine.
	// Nothing here touches an MP signal, an RPC, squad scope or any server-owned state - the data
	// link setters below change what this client OFFERS and RECEIVES, and the server keeps deciding
	// what it is allowed to do with either.
	//------------------------------------------------------------------------------------------------
	void SetCivilianIffEnabled(bool enabled) { m_bRtCivilianIff = enabled; }
	void SetShowUnknownContacts(bool enabled) { m_bRtUnknown = enabled; }
	void SetContactFilter(ACM_TDContactFilter filter) { m_eRtContactFilter = filter; }
	void SetContactDetail(ACM_TDContactDetail detail) { m_eRtContactDetail = detail; }
	void SetShowStaleContacts(bool show) { m_bRtShowStale = show; }
	void SetFriendlyIffEnabled(bool enabled) { m_bRtFriendlyIff = enabled; }
	void SetFriendlyIffMode(ACM_TDFriendlyIffMode mode) { m_eRtFriendlyIffMode = mode; }
	void SetIffDetail(ACM_TDIffDetail detail) { m_eRtIffDetail = detail; }
	void SetOutlinesFriendlies(bool enabled) { m_bRtOutlineFriendlies = enabled; }
	void SetOutlinesConfirmed(bool enabled) { m_bRtOutlineHostiles = enabled; }
	void SetDrawsLabel(bool enabled) { m_bRtDrawLabel = enabled; }
	void SetLabelShowsDistance(bool enabled) { m_bRtLabelDistance = enabled; }
	void SetLabelShowsName(bool enabled) { m_bRtLabelName = enabled; }
	void SetHudEnabled(bool enabled) { m_bRtHud = enabled; }

	//------------------------------------------------------------------------------------------------
	// Hostile acquisition off. Classification keeps running and shared contacts keep arriving and
	// being stored - this stops them being ACQUIRED and DRAWN, it does not make the device deaf.
	//------------------------------------------------------------------------------------------------
	void SetHostileTrackingEnabled(bool enabled)
	{
		m_bRtHostileTracking = enabled;
	}

	//------------------------------------------------------------------------------------------------
	void SetMarkerScalePreset(ACM_TDMarkerScalePreset preset)
	{
		m_eRtMarkerScale = preset;
	}

	//------------------------------------------------------------------------------------------------
	// DATA LINK MASTER
	//
	// A parent gate, and nothing more. It does NOT write to the two intents underneath it, which is
	// the entire behaviour that was asked for: switch the link off, switch it back on, and receive
	// and share are exactly where the wearer left them.
	//------------------------------------------------------------------------------------------------
	void SetDataLinkEnabled(bool enabled)
	{
		m_bRtDataLink = enabled;
	}

	void SetShareIntent(bool enabled) { m_bRtShareIntent = enabled; }
	void SetReceiveIntent(bool enabled) { m_bRtReceiveIntent = enabled; }

	//------------------------------------------------------------------------------------------------
	// IVAS MASTER
	//
	// Everything augmented goes down: markers of every classification, data link presentation, and
	// the remote feed. Night vision and thermal are deliberately untouched - they are separate
	// hardware with their own switches, and a master that killed them would make this entry a
	// "turn the goggles off" button by accident.
	//
	// Nothing is cleared on the way down, so coming back up restores the wearer's settings without
	// any of them having to be saved anywhere.
	//------------------------------------------------------------------------------------------------
	void SetIvasEnabled(bool enabled)
	{
		if (m_bRtIvas == enabled)
			return;

		m_bRtIvas = enabled;

		ACM_RemoteFeedRenderer renderer = ACM_RemoteFeedRenderer.GetInstance();

		if (!enabled)
		{
			m_bFeedWasActiveIvas = renderer.IsActive();
			renderer.Stop();

			// The master hides everything augmented, and the visor frame is part of that. Leaving it
			// up with nothing behind it would be the most obviously broken possible state.
			ApplyVisorPresentation(false);
			return;
		}

		if (m_bFeedWasActiveIvas && m_bVisorDown)
		{
			PushDisplayRegionToFeed();
			renderer.Start();
		}

		m_bFeedWasActiveIvas = false;

		ApplyVisorPresentation(m_bVisorDown);
	}

	//------------------------------------------------------------------------------------------------
	void SetFeedSizePreset(ACM_TDFeedSizePreset preset)
	{
		m_eRtFeedSize = preset;
		PushFeedPresets();
	}

	//------------------------------------------------------------------------------------------------
	void SetFeedPositionPreset(ACM_TDFeedPositionPreset preset)
	{
		m_eRtFeedPosition = preset;
		PushFeedPresets();
	}

	//------------------------------------------------------------------------------------------------
	// Translation lives here rather than in the renderer, so the renderer never learns that a
	// target-detection preset enum exists.
	//------------------------------------------------------------------------------------------------
	protected void PushFeedPresets()
	{
		ACM_RemoteFeedRenderer renderer = ACM_RemoteFeedRenderer.GetInstance();

		float scale = 1.0;
		if (m_eRtFeedSize == ACM_TDFeedSizePreset.SMALL)
			scale = 0.72;
		else if (m_eRtFeedSize == ACM_TDFeedSizePreset.LARGE)
			scale = 1.38;

		renderer.SetSizeScale(scale);

		ACM_ERemoteFeedCorner corner = ACM_ERemoteFeedCorner.TOP_LEFT;
		if (m_eRtFeedPosition == ACM_TDFeedPositionPreset.TOP_RIGHT)
			corner = ACM_ERemoteFeedCorner.TOP_RIGHT;
		else if (m_eRtFeedPosition == ACM_TDFeedPositionPreset.BOTTOM_LEFT)
			corner = ACM_ERemoteFeedCorner.BOTTOM_LEFT;
		else if (m_eRtFeedPosition == ACM_TDFeedPositionPreset.BOTTOM_RIGHT)
			corner = ACM_ERemoteFeedCorner.BOTTOM_RIGHT;

		renderer.SetCorner(corner);
	}

	//------------------------------------------------------------------------------------------------
	// Seed the runtime preferences from the authored attributes.
	//
	// Called once at post-init and again by a confirmed reset, which is why it is one method rather
	// than a constructor's worth of assignments: "reset" and "what this device ships with" have to
	// be the same list, or they drift and reset stops meaning anything.
	//------------------------------------------------------------------------------------------------
	protected void InitRuntimePreferences()
	{
		m_bRtIvas = m_bIvasEnabledDefault;
		m_bRtHud = m_bHudEnabledDefault;
		m_bRtFriendlyIff = m_bFriendlyIffEnabledDefault;
		m_bRtHostileTracking = m_bHostileTrackingDefault;
		m_bRtCivilianIff = m_bCivilianIff;
		m_bRtUnknown = m_bUnknownContacts;
		m_bRtOutlineFriendlies = m_bOutlineFriendlies;
		m_bRtOutlineHostiles = m_bIvasOutlineConfirmed;
		m_bRtShowStale = m_bShowStaleContacts;
		m_bRtDrawLabel = m_bDrawLabel;
		m_bRtLabelDistance = m_bLabelShowDistance;
		m_bRtLabelName = m_bLabelShowName;

		m_bRtDataLink = m_bDataLinkEnabledDefault;
		m_bRtShareIntent = m_bShareContacts;
		m_bRtReceiveIntent = m_bReceiveContacts;

		m_eRtFriendlyIffMode = m_eFriendlyIffMode;
		m_eRtIffDetail = m_eIffDetail;
		m_eRtContactFilter = m_eContactFilter;
		m_eRtContactDetail = m_eContactDetail;
		m_eRtMarkerScale = m_eMarkerScalePreset;
		m_eRtFeedSize = m_eFeedSizePreset;
		m_eRtFeedPosition = m_eFeedPositionPreset;
	}

	//------------------------------------------------------------------------------------------------
	// Confirmed from the radial. Scope is deliberately narrow: this device's own presentation
	// preferences and nothing else.
	//
	// It does NOT touch the tactical role registry, group membership, data link authorisation,
	// server-side network links or source ownership. Those are not this device's settings - they
	// belong to the server or to the player, and a display preference reset that quietly dropped a
	// squad assignment would be a genuinely nasty surprise.
	//------------------------------------------------------------------------------------------------
	void ResetIvasSettings()
	{
		bool wasIvasOn = m_bRtIvas;

		InitRuntimePreferences();
		PushFeedPresets();

		// Route a change of master state through the setter so the feed is started or stopped the
		// same way it would be from the menu, rather than left in whatever state it happened to be.
		if (wasIvasOn != m_bRtIvas)
		{
			bool wanted = m_bRtIvas;
			m_bRtIvas = wasIvasOn;
			SetIvasEnabled(wanted);
		}
	}

	// Named IsDetectionActive rather than IsActive: ScriptComponent already declares
	// IsActive(), and colliding with it is a compile error rather than an override.
	bool IsDetectionActive() { return m_bActive; }

	//------------------------------------------------------------------------------------------------
	Color GetAffiliationColor(ACM_TDAffiliation affiliation)
	{
		if (!m_bUseFactionColors)
			return m_ColUnknown;

		if (affiliation == ACM_TDAffiliation.FRIENDLY)
			return m_ColFriendly;

		if (affiliation == ACM_TDAffiliation.HOSTILE)
			return m_ColHostile;

		if (affiliation == ACM_TDAffiliation.CIVILIAN)
			return m_ColCivilian;

		if (affiliation == ACM_TDAffiliation.NEUTRAL)
			return m_ColNeutral;

		return m_ColUnknown;
	}

	//------------------------------------------------------------------------------------------------
	// Driven by ACM_NightVisionComponent when that component is present - it powers this on and off
	// alongside itself, and calls through on every one of its teardown paths (unequip, death, menu
	// open). On a device with NO night vision component this is driven by the standalone visor logic
	// below instead. Never called for remote players: both callers gate on the local player first.
	//------------------------------------------------------------------------------------------------
	void SetDetectionActive(bool active)
	{
		// The visor has the final say, whoever is calling.
		//
		// ACM_NightVisionComponent calls through here when it powers on, which without this gate
		// would raise the tactical HUD while the optic is still stowed on the wearer's forehead.
		// Turning night vision on is not the same as lowering the visor, and now it no longer
		// pretends to be.
		if (active && m_bHasVisor && !m_bVisorDown)
			return;

		// A device this machine does not control must never become the active provider.
		//
		// Belt to the braces on the callers: the night vision component already gates its call on the
		// local player. If anything ever stops doing that, the failure is the local HUD quietly
		// running off somebody else's goggles and their settings, which is very hard to recognise as
		// a bug from inside the game.
		if (active && m_bHasVisor && !m_bLocalAuthority)
			return;

		if (m_bActive == active)
			return;

		m_bActive = active;

		ACM_TargetDetectionSystem system = ACM_TargetDetectionSystem.GetInstance();
		if (!system)
			return;

		if (m_bActive)
			system.Activate(this);
		else
			system.Deactivate(this);
	}


	//------------------------------------------------------------------------------------------------
	// STANDALONE VISOR
	//
	// The visor is a property of the HARDWARE, not of any sensor bolted to it, and this component
	// owns it unconditionally.
	//
	// It did not, at first. The first attempt only took over when ACM_NightVisionComponent was absent,
	// on the theory that a full NVG device should keep its existing behaviour. That theory was wrong
	// about what the existing behaviour actually was: ACM_NightVisionComponent AND
	// ACM_ThermalComponentV2 each write w_nvRotator from their own power state, so on a normal IVAS
	// the visor was not a flip at all - it was a side effect of night vision powering up. You could
	// not lower the visor without turning NVG on, which is exactly backwards from how the hardware
	// works.
	//
	// THREE WRITERS, DELIBERATELY. The visor should always move: on the IVAS toggle, and on night
	// vision or thermal powering up. So m_sAnimationSignal stays populated on both of those
	// components rather than being cleared.
	//
	// The consequence is worth knowing rather than designing around: those two write the signal from
	// their own power state on every Update, while this component writes once per flip. A per-frame
	// writer beats a per-flip writer, so with night vision on, the visor follows night vision. That
	// is the intended behaviour here - the visor tracks whatever was most recently switched - and it
	// is why m_bVisorDown re-asserts itself in SetVisorDown when it finds the signal disagreeing.
	//
	// Visor position IS the IVAS power state: flipping up stops the scan, drops every contact and
	// clears the overlay. A raised optic showing a tactical picture would be the kind of detail that
	// quietly undermines everything else the system is careful about.
	//------------------------------------------------------------------------------------------------
	[Attribute("0", UIWidgets.CheckBox, desc: "This device's visor can be flipped by the IVAS toggle, in addition to being driven by night vision and thermal powering up. OFF for a conventional NVG. ON for an IVAS, which gets its own visor control on top of the optics. Leave m_sAnimationSignal set to the rotator signal on ACM_NightVisionComponent and ACM_ThermalComponentV2 in the same prefab - the visor is meant to move for all three. Note that those two write the signal every Update from their own power state, so whichever acted last wins: switching night vision on will raise or lower the visor to match its power state regardless of where the IVAS toggle last put it.", category: "ACM Target Detection - Visor")]
	protected bool m_bIsIvasVisor;

	protected static const string ROTATOR_SIGNAL = "w_nvRotator";

	// Capability vs authority - not the same thing.
	//
	// m_bHasVisor: this device has a flip-down visor. True everywhere, for every IVAS.
	// m_bLocalAuthority: this machine may drive it. True on one machine, for one device.
	//
	// The rotator is an MP signal, so it replicates. Collapsing the two meant every client wrote
	// that signal for every IVAS it could see, which flipped everybody's visor at once.
	protected bool m_bHasVisor;
	protected bool m_bLocalAuthority;

	// TEMPORARY. Traces every step of the visor path so the next failure is read rather than guessed.
	protected static const bool ACM_VISOR_DEBUG = true;

	// Countdown for the post-flip signal watcher below.
	protected int m_iVisorSamples;

	// What it was doing when authority went away, so returning restores it rather than defaulting.
	protected bool m_bFeedWasActive;

	// Every device in this process, so a change of body re-decides all of them at once.
	protected static ref array<ACM_TargetDetectionComponent> s_aDevices;
	protected bool m_bVisorDown;
	protected bool m_bInputBound;
	protected SignalsManagerComponent m_pSignals;
	protected int m_iRotatorSignal = -1;

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);

		// Deferred: sibling components are not reliably resolvable at post-init, and asking now
		// would report "no night vision component" on a device that has one.
		if (!s_aDevices)
			s_aDevices = new array<ACM_TargetDetectionComponent>();

		if (s_aDevices.Find(this) == -1)
			s_aDevices.Insert(this);

		// Immediately, not deferred. The scan and draw passes read these through the accessors and
		// a device can become the active provider before any deferred work has run.
		InitRuntimePreferences();

		GetGame().GetCallqueue().CallLater(ResolveVisorOwnership, 750, false);
	}

	//------------------------------------------------------------------------------------------------
	// The device this player is actually operating, or null.
	//
	// The radial menu's single entry point. Authority first, active provider second, and the order
	// matters: with the visor stowed nothing is lit, so there is no active provider - and the menu
	// still has to be able to reach the device in order to lower the visor.
	//
	// Authority is the same test that gates the keybinds, so a helmet lying on the floor and another
	// player's IVAS both answer null here for exactly the reason they ignore the toggle key.
	//------------------------------------------------------------------------------------------------
	static ACM_TargetDetectionComponent GetLocalDevice()
	{
		if (s_aDevices)
		{
			foreach (ACM_TargetDetectionComponent device : s_aDevices)
			{
				if (device && device.HasLocalAuthority())
					return device;
			}
		}

		ACM_TargetDetectionSystem system = ACM_TargetDetectionSystem.GetInstance();
		if (system && system.GetActiveProvider())
			return system.GetActiveProvider();

		// Third fallback: a device the local character is demonstrably carrying, even though
		// authority has not been granted to it yet.
		//
		// Without this the radial menu is dark for up to a second after a teleport or a possession,
		// and dark for good if authority resolution is wrong about anything - the player is left with
		// working hardware and no way to reach it. Resolving the carrier directly here means the menu
		// can always find the device, and the entries themselves report whether they can act on it.
		IEntity local = SCR_PlayerController.GetLocalControlledEntity();
		if (!local || !s_aDevices)
			return null;

		foreach (ACM_TargetDetectionComponent carried : s_aDevices)
		{
			if (!carried)
				continue;

			IEntity owner = carried.GetOwner();
			if (!owner)
				continue;

			ChimeraCharacter carrier = ACM_InventoryUtils.FindCharacterOwnerFromEntity(owner);
			if (carrier && carrier == local)
				return carried;
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	// Re-decide authority for every device in this process, right now.
	//
	// Called when the radial menu opens. The poll would get there within a second anyway, but a
	// player who has just noticed the menu is missing its IVAS branch will reopen it immediately, and
	// having that reopen be the thing that fixes it is worth one inventory walk.
	//------------------------------------------------------------------------------------------------
	static void RefreshAllAuthority()
	{
		if (!s_aDevices)
			return;

		foreach (ACM_TargetDetectionComponent device : s_aDevices)
		{
			if (device)
				device.RefreshAuthority();
		}
	}


	//------------------------------------------------------------------------------------------------
	// The device a given character is carrying, on whatever machine is asking.
	//
	// GetLocalDevice answers for the local player only. This one answers for anybody, which is what
	// the replicated visor needs: every client has to find the same device on the same character in
	// order to write the same rotator signal.
	//------------------------------------------------------------------------------------------------
	static ACM_TargetDetectionComponent GetDeviceForCharacter(IEntity character)
	{
		if (!character || !s_aDevices)
			return null;

		foreach (ACM_TargetDetectionComponent device : s_aDevices)
		{
			if (!device || !device.m_bHasVisor)
				continue;

			IEntity owner = device.GetOwner();
			if (!owner)
				continue;

			ChimeraCharacter carrier = ACM_InventoryUtils.FindCharacterOwnerFromEntity(owner);
			if (carrier && carrier == character)
				return device;
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	// Apply a visor position that arrived over the network.
	//
	// NO AUTHORITY GATE, deliberately, and that is safe here in a way a raw signal write is not: the
	// only route to this method is a server broadcast, so the decision was already made by the
	// machine allowed to make it. The local-authority test lives on the REQUEST instead - see
	// SetVisorDown - which is where it belongs.
	//------------------------------------------------------------------------------------------------
	void ApplyVisorFromNetwork(bool down)
	{
		if (ACM_VISOR_DEBUG)
			Print("[ACM VISOR] fromNetwork down=" + down.ToString(), LogLevel.DEBUG);

		m_bVisorDown = down;

		// Polarity flipped with the bind-pose re-rig: 1 is DEPLOYED (+145), 0 is the stowed rest
		// pose. See ACM_NightVisionComponent.UpdateAnimation for the full explanation.
		//
		// Through the shared writer, NOT a raw SetSignalValue. This is the path every machine takes on
		// a dedicated server, and writing the signal without asserting ACTIVE first is why the visor
		// never moved on one. See WriteVisorSignal.
		WriteVisorSignal();

		// Scan and feed are this machine's business and only matter where the wearer is sitting.
		if (!m_bLocalAuthority)
			return;

		// Power-on splash, wearer's screen only. Fired from the transition rather than from the
		// input handler so it plays however the visor came down - keybind, radial, or user action -
		// and cannot play for somebody watching.
		if (m_bVisorDown)
			ACM_IvasBootSequence.Play(GetOwner());
		else
			ACM_IvasBootSequence.Abort(GetOwner());

		ApplyVisorPresentation(m_bVisorDown);

		SetDetectionActive(m_bVisorDown);
		ACM_RemoteFeedRenderer.GetInstance().OnVisorChanged(m_bVisorDown);
	}

	//------------------------------------------------------------------------------------------------
	bool HasLocalAuthority()
	{
		return m_bLocalAuthority;
	}

	//------------------------------------------------------------------------------------------------
	// Different body now - GM taking over an AI, or dropping back out. Authority moves with them.
	//------------------------------------------------------------------------------------------------
	static void OnLocalControlledEntityChanged()
	{
		if (!s_aDevices)
			return;

		foreach (ACM_TargetDetectionComponent device : s_aDevices)
		{
			if (device)
				device.RefreshAuthority();
		}
	}

	//------------------------------------------------------------------------------------------------
	// Every creator uses the same parameters, regardless of component init order.
	// IVAS already eases in ACM_V2_NVG.siga (700/600 ms). Slow MP blending must not
	// interpolate its target a second time. Conventional NVG settings are preserved.
	static int EnsureAnimationSignal(IEntity owner, SignalsManagerComponent signals, string signalName)
	{
		if (!owner || !signals)
			return -1;
		ACM_TargetDetectionComponent detection = ACM_TargetDetectionComponent.Cast(owner.FindComponent(ACM_TargetDetectionComponent));
		if (detection && detection.m_bIsIvasVisor)
			return signals.AddOrFindMPSignal(signalName, 0, 1000, 0);
		return signals.AddOrFindMPSignal(signalName, 0.01, 0.5, 0);
	}
	protected void ResolveVisorOwnership()
	{
		IEntity owner = GetOwner();
		if (!owner)
			return;

		// Opt-in per device, and off by default.
		//
		// A conventional NVG must keep its original behaviour, where the rotator is driven by night
		// vision powering up. Taking the visor away from every device that happens to have a signals
		// component would have broken the FPGPNVG family to fix the IVAS.
		if (!m_bIsIvasVisor)
			return;

		m_pSignals = SignalsManagerComponent.Cast(owner.FindComponent(SignalsManagerComponent));
		if (!m_pSignals)
		{
			// Used to return in silence, which made a misconfigured prefab indistinguishable from a
			// working one that simply refused to move. If the visor is switched on and the hardware
			// cannot support it, say so once.
			Print("[ACM IVAS] " + owner.ToString() + " has the IVAS visor enabled but no SignalsManagerComponent, so the rotator cannot be driven. The visor will never move.", LogLevel.WARNING);
			return;
		}

		// The graph supplies the flip easing; replication supplies the target pose.
		m_iRotatorSignal = EnsureAnimationSignal(owner, m_pSignals, ROTATOR_SIGNAL);
		if (m_iRotatorSignal < 0)
		{
			Print("[ACM IVAS] " + owner.ToString() + " could not create the MP signal '" + ROTATOR_SIGNAL + "'. The visor will never move - check that the model's animation graph declares it.", LogLevel.WARNING);
			return;
		}

		// Capability only. NOTHING is written to the signal here - this runs on every machine for
		// every IVAS in the world, and writing from all of them is the bug.
		m_bHasVisor = true;

		// ACTIVE is a different matter and IS asserted everywhere, exactly as
		// ACM_NightVisionComponent does on its own init. It writes no value and expresses no opinion
		// about where the visor sits - it only makes the entity simulable, so that whenever a value
		// does arrive there is something running to consume it. Without this a proxy streams in
		// inactive and the first broadcast lands on a bone nobody is evaluating.
		if ((owner.GetFlags() & EntityFlags.ACTIVE) == 0)
			owner.SetFlags(EntityFlags.ACTIVE, false);

		ResolveVisorAuthority();
	}


	//------------------------------------------------------------------------------------------------
	// Same test the input binding used, because it is the same question. Retried rather than
	// answered once - gear is picked up and swapped long after the component initialised.
	//------------------------------------------------------------------------------------------------
	protected void ResolveVisorAuthority()
	{
		RefreshAuthority();
		EnsureDetectionResumed();

		// Safety net, and it carries more weight than the name suggests.
		//
		// The controlled-entity hook only fires when the local player changes BODY. It does not fire
		// when the same character is teleported, respawned into, or has their loadout rebuilt - and
		// all three can move the gear in the inventory tree. This poll is the only thing that
		// notices.
		//
		// One second rather than three. The check is an inventory walk bounded at fifteen links, run
		// for the handful of ACM devices in the process, so the cost is nothing next to a three-second
		// window in which the goggles do not respond.
		GetGame().GetCallqueue().Remove(ResolveVisorAuthority);
		GetGame().GetCallqueue().CallLater(ResolveVisorAuthority, 1000, true);
	}

	//------------------------------------------------------------------------------------------------
	// Self-healing resume, run from the 1 Hz authority poll.
	//
	// Detection is switched off by several transient events (menu open, pause, GM camera during a
	// teleport, controlled-entity flicker) and switched back on by whichever event happened to
	// tear it down. When the tear-down and the resume come from different callers - the NVG
	// component saw the menu open while GetLocalControlledEntity() was null, the visor poll saw
	// the authority change - nobody re-arms it and the wearer has to cycle the goggles by hand.
	// This check reasserts the intended state from first principles instead of relying on the
	// event that caused the drop to also cause the resume.
	//------------------------------------------------------------------------------------------------
	protected void EnsureDetectionResumed()
	{
		IEntity owner = GetOwner();
		if (!owner)
			return;

		// Only the wearer's machine draws; a device carried by a remote player never resumes here.
		if (m_bHasVisor && !m_bLocalAuthority)
			return;

		if (m_bHasVisor && !m_bVisorDown)
			return;

		if (!m_bHasVisor)
		{
			IEntity localCharacter = SCR_PlayerController.GetLocalControlledEntity();
			if (!localCharacter || !IsCarriedBy(owner, localCharacter))
				return;
		}

		// A menu being open is the one legitimate reason to stay dark - DrawTick tears the overlay
		// down for it and it must not be fought here.
		MenuManager menuManager = GetGame().GetMenuManager();
		if (menuManager && menuManager.IsAnyMenuOpen())
			return;

		// If a night vision component drives this device, follow its power state.
		ACM_NightVisionComponent nvg = ACM_NightVisionComponent.Cast(owner.FindComponent(ACM_NightVisionComponent));
		if (nvg && !nvg.IsNVGActive())
			return;

		if (!m_bActive)
		{
			SetDetectionActive(true);
			return;
		}

		// Active on paper but the system is not ticking (Start() bailed on a missing workspace, or
		// another provider was deactivated and the hand-over lost us). Re-register.
		ACM_TargetDetectionSystem system = ACM_TargetDetectionSystem.GetInstance();
		if (system && (!system.IsRunning() || system.GetProvider() != this))
			system.Activate(this);
	}

	//------------------------------------------------------------------------------------------------
	// Grants and revokes. A one-way resolve left helmets obeying players who had left them.
	//------------------------------------------------------------------------------------------------
	void RefreshAuthority()
	{
		if (!m_bHasVisor)
			return;

		IEntity owner = GetOwner();
		IEntity localCharacter = SCR_PlayerController.GetLocalControlledEntity();

		// No controlled character is the GM case - a camera wears nothing, so nothing is owned.
		bool shouldOwn = false;
		if (owner && localCharacter)
			shouldOwn = IsCarriedBy(owner, localCharacter);

		if (shouldOwn == m_bLocalAuthority)
			return;

		if (shouldOwn)
			GainAuthority();
		else
			LoseAuthority();
	}

	//------------------------------------------------------------------------------------------------
	// m_bVisorDown lives on the component, so the device remembered its own position while
	// unattended.
	//------------------------------------------------------------------------------------------------
	protected void GainAuthority()
	{
		m_bLocalAuthority = true;

		ApplyVisorSignal();
		BindInput();

		SetDetectionActive(m_bVisorDown);

		if (m_bFeedWasActive && m_bVisorDown)
			ACM_RemoteFeedRenderer.GetInstance().Start();

		m_bFeedWasActive = false;
	}

	//------------------------------------------------------------------------------------------------
	// The visor signal is NOT written here. Walking away from someone else's helmet should leave it
	// exactly as it sits. Only this machine's view is torn down: input, local scan, feed.
	//------------------------------------------------------------------------------------------------
	protected void LoseAuthority()
	{
		m_bLocalAuthority = false;

		ACM_RemoteFeedRenderer renderer = ACM_RemoteFeedRenderer.GetInstance();
		m_bFeedWasActive = renderer.IsActive();
		renderer.Stop();

		UnbindInput();
		SetDetectionActive(false);

		ACM_IvasBootSequence.Abort(GetOwner());
		ApplyVisorPresentation(false);
	}

	//------------------------------------------------------------------------------------------------
	protected void UnbindInput()
	{
		if (!m_bInputBound)
			return;

		InputManager im = GetGame().GetInputManager();
		if (im)
		{
			im.RemoveActionListener("ACM_IVAS_Toggle", EActionTrigger.DOWN, OnToggleVisorInput);
			im.RemoveActionListener("ACM_RemoteFeed_Toggle", EActionTrigger.DOWN, OnToggleRemoteFeedInput);
			im.RemoveActionListener("ACM_RemoteFeed_Cycle", EActionTrigger.DOWN, OnCycleRemoteFeedInput);
		}

		m_bInputBound = false;
	}

	//------------------------------------------------------------------------------------------------
	// Input is bound only for the local player, and only for a device this player is actually
	// carrying - otherwise every standalone IVAS lying on the ground would answer the keybind.
	//------------------------------------------------------------------------------------------------
	protected void BindInput()
	{
		if (m_bInputBound)
			return;

		// RefreshAuthority is the only place that decides whether this device is ours.
		if (!m_bLocalAuthority)
			return;

		InputManager im = GetGame().GetInputManager();
		if (!im)
			return;

		im.AddActionListener("ACM_IVAS_Toggle", EActionTrigger.DOWN, OnToggleVisorInput);

		// The device is the receiver, so the feed keybinds belong here rather than on the renderer.
		im.AddActionListener("ACM_RemoteFeed_Toggle", EActionTrigger.DOWN, OnToggleRemoteFeedInput);
		im.AddActionListener("ACM_RemoteFeed_Cycle", EActionTrigger.DOWN, OnCycleRemoteFeedInput);

		m_bInputBound = true;
	}

	//------------------------------------------------------------------------------------------------
	// Is this device on that character?
	//
	// THE PARENT WALK ALONE WAS THE BUG. Worn and slotted equipment is NOT parented to the character
	// in the scene hierarchy - it lives in inventory slots, and GetParent() returns null part-way up.
	// We already learned this the hard way in the remote feed source component, where a helmet-mounted
	// camera resolved its carrier as itself and put the feed camera at the world origin.
	//
	// So the same trap was sitting here, deciding whether this machine may drive the visor. It happens
	// to answer correctly for a weapon-rail device, because attachment slots DO parent - which is
	// exactly why it survived testing. A helmet-mounted IVAS in a head-cover slot could fail it, and
	// anything that re-slots the gear can flip the answer without warning. A teleport does that.
	//
	// The consequence of a false negative is the whole system going quiet: no authority means no
	// visor, no scan, no feed and no IVAS entries in the radial. "It just stops working sometimes,
	// especially after teleporting" is what that looks like from the outside.
	//
	// Inventory chain first, hierarchy walk second. Both, because the two mounting methods are both
	// real: gear is slotted, weapon attachments are parented.
	//------------------------------------------------------------------------------------------------
	protected bool IsCarriedBy(IEntity device, IEntity character)
	{
		if (!device || !character)
			return false;

		// Slotted or worn: walk the inventory chain up to whoever is carrying it.
		ChimeraCharacter carrier = ACM_InventoryUtils.FindCharacterOwnerFromEntity(device);
		if (carrier && carrier == character)
			return true;

		// Parented: weapon rails and anything genuinely attached in the scene hierarchy. Bounded so a
		// malformed hierarchy cannot spin here.
		IEntity walk = device;
		int guard = 0;
		while (walk && guard < 16)
		{
			if (walk == character)
				return true;

			walk = walk.GetParent();
			guard++;
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected void OnToggleVisorInput()
	{
		// Same double-fire guard the rest of ACM uses for custom bindings.
		if (!ACM_InputBindingHelper.ShouldHandleInput("ACM_IVAS_Toggle"))
			return;

		SetVisorDown(!m_bVisorDown);
	}

	//------------------------------------------------------------------------------------------------
	//------------------------------------------------------------------------------------------------
	protected void OnToggleRemoteFeedInput()
	{
		if (!ACM_InputBindingHelper.ShouldHandleInput("ACM_RemoteFeed_Toggle"))
			return;

		// Visor up means the device is off. Nothing to display on.
		if (m_bHasVisor && !m_bVisorDown)
			return;

		Print("[ACM RemoteFeed] Toggle input received from device " + GetOwner().ToString(), LogLevel.DEBUG);

		// The feed lives inside this device's display, so the device tells the renderer where that
		// is before asking it to draw anything. Pushed on every toggle rather than once at startup
		// because the mask attributes are authored per device and the wearer can swap hardware.
		PushDisplayRegionToFeed();

		ACM_RemoteFeedRenderer.GetInstance().Toggle();
	}

	//------------------------------------------------------------------------------------------------
	// Steps the feed to the next available camera source. Only meaningful while the feed is up,
	// and pointless with the visor stowed.
	//------------------------------------------------------------------------------------------------
	protected void OnCycleRemoteFeedInput()
	{
		if (!ACM_InputBindingHelper.ShouldHandleInput("ACM_RemoteFeed_Cycle"))
			return;

		if (m_bHasVisor && !m_bVisorDown)
			return;

		ACM_RemoteFeedRenderer renderer = ACM_RemoteFeedRenderer.GetInstance();
		if (!renderer.IsActive())
			return;

		renderer.CycleSourceInput();
	}

	//------------------------------------------------------------------------------------------------
	// Translates this device's display mask into the shape vocabulary the renderer understands.
	// The translation belongs here, on the device, so the renderer never has to know that a
	// target detection display mask exists.
	//------------------------------------------------------------------------------------------------
	protected void PushDisplayRegionToFeed()
	{
		ACM_RemoteFeedRenderer renderer = ACM_RemoteFeedRenderer.GetInstance();

		renderer.SetRenderScale(m_fFeedRenderScale);
		renderer.SetReceiverGain(m_fFeedReceiverGain);
		renderer.SetDebugQuality(m_fFeedDebugQuality);
		renderer.SetRegionSizeFraction(m_fFeedSizeFraction);
		renderer.SetRegionMargin(m_fFeedRegionMargin);

		// The wearer's size and corner presets go with the region, not after it - pushing the region
		// alone would place the panel by the prefab's numbers for one frame and then move it.
		PushFeedPresets();

		ACM_RemoteFeedDisplayRegion region = new ACM_RemoteFeedDisplayRegion();

		// FULLSCREEN is how the renderer is told to dock to the screen instead of to a region -
		// it is the absence of a region, not a region the size of the screen.
		if (!m_bFeedInDisplayRegion)
			region.Set(ACM_ERemoteFeedRegionShape.FULLSCREEN, 0.5, 0.5, 0.5, 0.5);
		else if (m_eDisplayMask == ACM_TDDisplayMask.CIRCLE)
			region.Set(ACM_ERemoteFeedRegionShape.CIRCLE, m_fMaskCenterX, m_fMaskCenterY, m_fMaskRadius, m_fMaskRadius);
		else if (m_eDisplayMask == ACM_TDDisplayMask.RECTANGLE)
			region.Set(ACM_ERemoteFeedRegionShape.RECTANGLE, m_fMaskCenterX, m_fMaskCenterY, m_fMaskHalfWidth, m_fMaskHalfHeight);
		else
			region.Set(ACM_ERemoteFeedRegionShape.FULLSCREEN, m_fMaskCenterX, m_fMaskCenterY, 0.5, 0.5);

		renderer.SetDisplayRegion(region);
	}

	//------------------------------------------------------------------------------------------------
	// Visor frame and HUD suppression, together, because they are one visual state.
	//
	// Driven from the visor transition rather than from an input handler, so it follows however the
	// visor moved - keybind, radial, user action, or night vision powering up - and cannot engage for
	// somebody watching. Authority is already established by the time this is reached.
	//------------------------------------------------------------------------------------------------
	protected void ApplyVisorPresentation(bool down)
	{
		if (down && m_bRtIvas)
		{
			if (m_bVisorOverlay)
				ACM_IvasVisorOverlay.Show(GetOwner());

			if (m_bSuppressVanillaHud)
				ACM_VanillaHudSuppressor.Suppress(m_sSuppressedHudClasses, m_bSuppressChat);

			RefreshFeedPlacement();
			return;
		}

		ACM_IvasVisorOverlay.Hide();
		ACM_VanillaHudSuppressor.Restore();

		RefreshFeedPlacement();
	}

	//------------------------------------------------------------------------------------------------
	// The remote feed docks to a screen corner, and the visor now owns bands at the top and bottom
	// of that screen. Told AFTER the overlay has been shown or hidden, because the overlay's own
	// shown state is what the feed measures its safe area from - asking it first would place the
	// panel against the state that is about to change.
	//------------------------------------------------------------------------------------------------
	protected void RefreshFeedPlacement()
	{
		ACM_RemoteFeedRenderer renderer = ACM_RemoteFeedRenderer.GetInstance();
		if (renderer)
			renderer.RefreshPlacement();
	}

	//------------------------------------------------------------------------------------------------
	// Public entry point for the radial, which has to push the region before starting the feed for
	// the same reason the keybind does: the panel lives inside this device's display, and that
	// geometry is authored per device.
	//------------------------------------------------------------------------------------------------
	void PushFeedGeometry()
	{
		PushDisplayRegionToFeed();
	}

	//------------------------------------------------------------------------------------------------
	void SetVisorDown(bool down)
	{
		if (ACM_VISOR_DEBUG)
		{
			string r = "[ACM VISOR] request down=";
			r += down.ToString();
			r += " was=";
			r += m_bVisorDown.ToString();
			r += " authority=";
			r += m_bLocalAuthority.ToString();
			r += " rpl=";
			r += Replication.IsRunning().ToString();
			Print(r, LogLevel.DEBUG);
		}

		// Security boundary. Without it any caller on any machine drives any wearer's visor.
		if (!m_bLocalAuthority)
			return;

		// Already there as far as this component knows - but check the signal before believing it.
		//
		// The user action used to write the rotator directly without telling anyone, so the component
		// could think the visor was down while it was actually up. The next toggle then wrote the
		// value that was already there and nothing moved. Re-asserting on a mismatch makes that
		// recoverable instead of sticky.
		if (m_bVisorDown == down)
		{
			if (m_pSignals && m_iRotatorSignal >= 0)
			{
				// Polarity flipped with the bind-pose re-rig: down (deployed) is now 1, up is 0.
				float expected = 0;
				if (m_bVisorDown)
					expected = 1;

				if (m_pSignals.GetSignalValue(m_iRotatorSignal) != expected)
					ApplyVisorSignal();
			}

			return;
		}

		// Applied locally first so the flip feels immediate, then asked of the server so it reaches
		// everybody else. The broadcast re-applies the same value here, which is a no-op.
		ApplyVisorFromNetwork(down);

		// THE REASON THIS IS NOT JUST A LOCAL SIGNAL WRITE.
		//
		// w_nvRotator is an MP signal on an item the SERVER owns. A client writing it changes nothing
		// anyone else can see, and on a dedicated server it does not even move the visor for the
		// player who pressed the key - which is exactly the "works in the editor, dead on a server"
		// report.
		//
		// Night vision and thermal never had this problem because ACM_NightVisionComponent is an
		// SCR_GadgetComponent: its activated state replicates through the gadget system, and every
		// machine independently derives the signal from it. The visor is the one part of this device
		// with no such backing, so it needs the pipe.
		//
		// Routed through ACM_PlayerLinkComponent because that lives on the character, which the
		// client owns - the server can therefore derive who is asking instead of taking their word
		// for it. Sending from the item would be sending from an entity the client does not own.
		ACM_PlayerLinkComponent link = ACM_PlayerLinkComponent.GetLocal();
		if (link)
			link.AskSetVisor(down);
	}

	//------------------------------------------------------------------------------------------------
	bool IsVisorDown()
	{
		return m_bVisorDown;
	}

	//------------------------------------------------------------------------------------------------
	// Whether this device presents a visor the wearer can flip. Read by the radial menu so the entry
	// only appears on hardware that actually has one.
	//------------------------------------------------------------------------------------------------
	bool HasIvasVisor()
	{
		// Capability AND control. Used by code that is about to ACT on the visor.
		return m_bHasVisor && m_bLocalAuthority;
	}

	//------------------------------------------------------------------------------------------------
	// Capability alone: this device has a visor, whoever is currently allowed to move it.
	//
	// The radial menu builds from this rather than from HasIvasVisor, and the distinction is a bug
	// fix. Gating the menu on authority meant that any moment authority had not resolved yet - the
	// second after a teleport, the tick after a possession - the visor entry vanished, and with it the
	// only way to switch the device on. The entry now always appears on hardware that has a visor and
	// reports NO CONTROL when this machine may not drive it, which is both usable and honest.
	//------------------------------------------------------------------------------------------------
	bool HasVisorHardware()
	{
		return m_bHasVisor;
	}

	//------------------------------------------------------------------------------------------------
	void ToggleVisor()
	{
		SetVisorDown(!m_bVisorDown);
	}

	//------------------------------------------------------------------------------------------------
	// Signal polarity matches ACM_NightVisionComponent.UpdateAnimation, which since the bind-pose
	// re-rig writes its activated state DIRECTLY: 1 is deployed (+145), 0 is the stowed rest pose.
	// Getting this backwards would animate the visor the wrong way round on standalone devices
	// only, which is exactly the sort of bug that survives a quick test with the NVG variant.
	//------------------------------------------------------------------------------------------------
	protected void ApplyVisorSignal()
	{
		// The signal replicates, so a write from a machine that does not own this device changes it
		// for the person who does.
		if (!m_bLocalAuthority)
			return;

		WriteVisorSignal();
	}

	//------------------------------------------------------------------------------------------------
	// THE SIGNAL WRITE, AND THE ACTIVE ASSERTION THAT MAKES IT MEAN ANYTHING.
	//
	// Split out because it was living inside ApplyVisorSignal, and ApplyVisorSignal is NOT the path a
	// networked visor flip takes. On a dedicated server the flip travels client -> Rpc_AskSetVisor ->
	// Rpc_DoSetVisor -> ApplyVisorFromNetwork, and that function wrote the signal raw, with no ACTIVE
	// assertion anywhere in it. The write landed on every machine and not one of them moved.
	//
	// This is the same defect that hid the animation in the first place: a worn or slotted item sits
	// with EntityFlags.ACTIVE cleared, an inactive entity never simulates, and an entity that never
	// simulates never re-evaluates its ProcAnimComponent. The signal changes underneath a bone that
	// nothing is reading.
	//
	// Night vision never showed the bug because ACM_NightVisionComponent asserts ACTIVE in two places
	// - on init and immediately before every write - so an NVG entity is simulable on every machine
	// from the moment it streams in. The visor had the assertion in exactly one place, on the one path
	// a server flip does not use. That asymmetry is the whole of "works in Workbench, dead on a
	// server": in the editor Replication.IsRunning() is false, the RPC is skipped, and the local write
	// runs on an entity whose night vision component had already asserted ACTIVE for its own reasons.
	//
	// Asserted before every write rather than once on init because the storage system clears the flag
	// again on re-slotting and inventory shuffles. It is idempotent and costs nothing.
	//------------------------------------------------------------------------------------------------
	protected void WriteVisorSignal()
	{
		if (!m_pSignals || m_iRotatorSignal < 0)
			return;

		IEntity visorOwner = GetOwner();
		if (visorOwner && (visorOwner.GetFlags() & EntityFlags.ACTIVE) == 0)
			visorOwner.SetFlags(EntityFlags.ACTIVE, false);

		// Keep the inventory item's animation active through the whole graph transition.
		if (visorOwner)
		{
			InventoryItemComponent item = InventoryItemComponent.Cast(visorOwner.FindComponent(InventoryItemComponent));
			if (item)
				item.ActivateOwner(true);
		}
		m_pSignals.SetSignalValue(m_iRotatorSignal, m_bVisorDown);

		// THE DECISIVE READ.
		//
		// If this writes 1 and reads back 0, the write is being refused or reverted - which is what a
		// client writing an MP signal on a server-owned item looks like, and it would mean no amount
		// of ACTIVE assertion will ever help.
		// visorOwner is null-checked above; the debug block dereferences it, so it is guarded here too.
		if (ACM_VISOR_DEBUG && visorOwner)
		{
			string w = "[ACM VISOR] write down=";
			w += m_bVisorDown.ToString();
			w += " readback=";
			w += m_pSignals.GetSignalValue(m_iRotatorSignal).ToString();
			w += " active=";
			w += ((visorOwner.GetFlags() & EntityFlags.ACTIVE) != 0).ToString();
			w += " authority=";
			w += m_bLocalAuthority.ToString();

			// The two components reach the signal by different calls. If these ids disagree there
			// are two signals with one name and the visor is writing the one nothing reads.
			w += " id=";
			w += m_iRotatorSignal.ToString();
			w += " findId=";
			w += m_pSignals.FindSignal(ROTATOR_SIGNAL).ToString();
			w += " procAnim=";
			w += (visorOwner.FindComponent(ProcAnimComponent) != null).ToString();
			Print(w, LogLevel.DEBUG);
			ArmVisorSampler();

			// INTERROGATE THE PROCANIM COMPONENT ITSELF.
			//
			// Everything so far has been inferred from outside it: the signal is right, the bone is in the
			// model, the .pap compiles, the prefab matches a working mod. None of that proves the component
			// on THIS entity actually loaded the graph. BaseProcAnimComponent can be asked directly.
			//
			//   controllers=0            -> the .pap never loaded; ResourceName or import is the fault
			//   signals list empty       -> graph loaded but exposes no input to drive
			//   w_nvRotator absent       -> the signal we write is not the one the graph listens to
			//   bones list empty         -> graph loaded but bound to no bone on this entity
			ProcAnimComponent pac = ProcAnimComponent.Cast(visorOwner.FindComponent(ProcAnimComponent));
			if (!pac)
			{
				Print("[ACM PROCANIM] no ProcAnimComponent on " + visorOwner.ToString(), LogLevel.DEBUG);
			}
			else
			{
				int ctrls = pac.GetControllerCount();

				string pa = "[ACM PROCANIM] controllers=";
				pa += ctrls.ToString();
				Print(pa, LogLevel.DEBUG);

				for (int ci = 0; ci < ctrls; ci++)
				{
					array<string> sigNames = {};
					pac.GetSignals(ci, sigNames);

					array<string> boneNames = {};
					pac.GetBoneNames(ci, boneNames);

					string cl = "[ACM PROCANIM] ctrl";
					cl += ci.ToString();
					cl += " signals=";
					foreach (string sn : sigNames)
					{
						cl += sn;
						cl += ",";
					}
					cl += " bones=";
					foreach (string bn : boneNames)
					{
						cl += bn;
						cl += ",";
					}
					cl += " idxOfOurSignal=";
					cl += pac.GetSignalIndex(ci, ROTATOR_SIGNAL).ToString();
					Print(cl, LogLevel.DEBUG);
				}
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	// Safety net: if the device entity is destroyed while lit (dropped and streamed out,
	// destroyed with the body) the system must not be left holding a dangling provider.
	//------------------------------------------------------------------------------------------------
	//------------------------------------------------------------------------------------------------
	// STREAM-IN.
	//
	// ACM_NightVisionComponent does exactly this and the visor had no equivalent, which is the second
	// half of why a dedicated server behaved differently from the editor: in the editor nothing ever
	// streams in, so the gap could not show.
	//
	// The 300ms is animation-specific rather than superstition. On stream-in the entity is still being
	// assembled and the skeleton the rotator bone lives on is not necessarily bound yet, so asserting
	// on the next frame writes into something that is not ready and silently does nothing.
	//------------------------------------------------------------------------------------------------
	protected override bool RplLoad(ScriptBitReader reader)
	{
		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().CallLater(EnsureVisorTicking, 300, false);

		return super.RplLoad(reader);
	}

	//------------------------------------------------------------------------------------------------
	// Make a streamed-in device simulable. NOTHING ELSE.
	//
	// The first version of this adopted the signal value into m_bVisorDown, on the reasoning that a
	// fresh proxy holds a default while the signal already carries the truth. That reasoning is fine
	// for a proxy and WRONG for the wearer, and RplLoad is not the one-shot stream-in hook I took it
	// for - it runs on state sync. So on the wearer's own machine every sync armed a callback that
	// 300ms later could overwrite the state they had just set with a signal value that had not caught
	// up, leaving the component believing the visor was up while it was down. The next toggle then
	// wrote the value that was already there and nothing moved. That is a self-inflicted version of
	// the exact bug SetVisorDown documents.
	//
	// So: the machine holding authority keeps its own state and merely re-asserts it. Only a proxy -
	// which has no opinion worth defending - adopts the signal, and that is what fixes the late
	// joiner who would otherwise see everybody with their optics up.
	//------------------------------------------------------------------------------------------------
	//------------------------------------------------------------------------------------------------
	// TEMPORARY. After a flip, watch the signal for two seconds.
	//
	// The readback immediately after the write already proved the value LANDS - it came back 1 with
	// the entity ACTIVE and authority held. What a single readback cannot show is whether something
	// writes over it a moment later, and on an IVAS device there is a SECOND WRITER on this exact
	// signal: ACM_NightVisionComponent.UpdateAnimation writes w_nvRotator from its own activated
	// state. With night vision switched off that is a 0. If it runs any time after the visor comes
	// down, the visor goes back up and nothing in the visor code did it.
	//
	// A plain NVG has only one writer, which would explain why it behaves and the IVAS does not,
	// despite identical prefab configuration.
	//------------------------------------------------------------------------------------------------
	protected void ArmVisorSampler()
	{
		if (!ACM_VISOR_DEBUG || !GetGame() || !GetGame().GetCallqueue())
			return;

		m_iVisorSamples = 20;
		GetGame().GetCallqueue().Remove(SampleVisorSignal);
		GetGame().GetCallqueue().CallLater(SampleVisorSignal, 100, true);
	}

	//------------------------------------------------------------------------------------------------
	protected void SampleVisorSignal()
	{
		if (!m_pSignals || m_iRotatorSignal < 0 || m_iVisorSamples <= 0)
		{
			if (GetGame() && GetGame().GetCallqueue())
				GetGame().GetCallqueue().Remove(SampleVisorSignal);

			return;
		}

		m_iVisorSamples--;

		string sv = "[ACM VISOR] sample value=";
		sv += m_pSignals.GetSignalValue(m_iRotatorSignal).ToString();
		sv += " believe=";
		sv += m_bVisorDown.ToString();
		Print(sv, LogLevel.DEBUG);
	}
	protected void EnsureVisorTicking()
	{
		IEntity owner = GetOwner();
		if (!owner || !m_bHasVisor || !m_pSignals || m_iRotatorSignal < 0)
			return;

		// The whole point of the deferred call: an inactive entity never ticks its ProcAnimComponent,
		// so whatever value arrives has nothing evaluating it.
		if ((owner.GetFlags() & EntityFlags.ACTIVE) == 0)
			owner.SetFlags(EntityFlags.ACTIVE, false);

		if (m_bLocalAuthority)
		{
			// Ours. Re-assert what we hold; never take dictation from a signal we are the source of.
			WriteVisorSignal();
			return;
		}

		// Somebody else's device. The signal is the only truth available here.
		m_bVisorDown = m_pSignals.GetSignalValue(m_iRotatorSignal) >= 0.5;
	}
	override void OnDelete(IEntity owner)
	{
		// Forget rather than Abort: the device itself is going away, so its boot record goes with it.
		ACM_IvasBootSequence.Forget(owner);
		// Only the device this machine drives may tear down the local visor/HUD; a remote
		// player's IVAS streaming out must not hide the local wearer's overlay.
		if (m_bLocalAuthority)
			ApplyVisorPresentation(false);

		UnbindInput();

		if (GetGame() && GetGame().GetCallqueue())
		{
			GetGame().GetCallqueue().Remove(ResolveVisorAuthority);
			GetGame().GetCallqueue().Remove(ResolveVisorOwnership);
			GetGame().GetCallqueue().Remove(EnsureVisorTicking);
			GetGame().GetCallqueue().Remove(SampleVisorSignal);
		}

		if (s_aDevices)
		{
			int index = s_aDevices.Find(this);
			if (index != -1)
				s_aDevices.Remove(index);
		}

		if (m_bActive)
		{
			m_bActive = false;

			ACM_TargetDetectionSystem system = ACM_TargetDetectionSystem.GetInstance();
			if (system)
				system.Deactivate(this);
		}

		// The renderer owns a camera entity. Losing the device without releasing it would leave
		// that camera alive with nothing pointing at it.
		if (m_bLocalAuthority)
			ACM_RemoteFeedRenderer.GetInstance().Stop();

		super.OnDelete(owner);
	}
}
