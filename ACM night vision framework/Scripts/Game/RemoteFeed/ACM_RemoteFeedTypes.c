// ACM IVAS Remote Feed - shared types
//
// Nothing in this file knows what a UAV or a K9 is, and nothing may be added here that does.
// Source characteristics live on ACM_RemoteFeedSourceComponent and its profile.

//------------------------------------------------------------------------------------------------
// How the feed behaves when a vanilla PiP weapon optic is active at the same time. Whichever way
// the coexistence test lands, the fix is a value here rather than restructuring the renderer.
//------------------------------------------------------------------------------------------------
enum ACM_ERemoteFeedArbitration
{
	// Both surfaces render. Preferred, and the default until the test says otherwise.
	COEXIST,

	// The weapon optic always wins. The feed holds its last frame, stops refreshing and shows a
	// status line until the player comes out of sights.
	SUSPEND_ON_ADS,
}

//------------------------------------------------------------------------------------------------
// Screen corner the feed docks to. Offsets are applied inwards from the chosen corner.
//------------------------------------------------------------------------------------------------
enum ACM_ERemoteFeedCorner
{
	TOP_LEFT,
	TOP_RIGHT,
	BOTTOM_LEFT,
	BOTTOM_RIGHT,
}

//------------------------------------------------------------------------------------------------
// What the renderer is actually doing right now, as opposed to what was asked of it.
//
// STOPPED and SUSPENDED are not the same thing and must not be collapsed. STOPPED releases the
// camera entity outright; SUSPENDED keeps it but parks the render target. Only STOPPED is
// guaranteed to cost nothing.
//------------------------------------------------------------------------------------------------
enum ACM_ERemoteFeedState
{
	STOPPED,
	RENDERING,
	SUSPENDED,
}

//------------------------------------------------------------------------------------------------
// Labelling only. No code may branch on this - behaviour that differs by source type belongs in an
// authored attribute instead.
//------------------------------------------------------------------------------------------------
enum ACM_ERemoteFeedSourceType
{
	GENERIC,
	UAV,
	K9,
	// A camera worn by a person - an IVAS visor cam, a bodycam. Man-portable at BOTH ends, which
	// is what makes it the harshest link in the list rather than the easiest.
	SOLDIER,
	VEHICLE,
	STATIC,
	MAST,
}

// Who may watch this camera.
//
// Authored per source. There is no implicit rule that an unowned camera is everyone's camera - a
// source with no operator falls back to this, and the default is the narrow end.
//------------------------------------------------------------------------------------------------
enum ACM_ERemoteFeedAccess
{
	// Operator only. An unattributed source under this policy is viewable by nobody, which is the
	// correct answer for a drone whose operator has disconnected.
	CONTROLLER_ONLY,
	// Operator plus their tactical group. Unattributed sources fall to the group that owns the
	// source's faction, which in practice means nobody until one is assigned.
	SQUAD,
	// Only viewers granted access through the data link's explicit group links.
	EXPLICIT_LINK,
	// Anyone on the source's own side.
	FACTION,
	// Deliberately open. A range camera, a training feed, a scenario prop.
	PUBLIC,
}

//------------------------------------------------------------------------------------------------
// Whose skeleton the bone name is looked up on.
//------------------------------------------------------------------------------------------------
enum ACM_ERemoteFeedBoneSource
{
	// Try the component's own entity first, then whatever is carrying it. Right for almost
	// everything: a device's own bone wins when it has one, and a character bone still resolves.
	AUTO,
	// Only this entity's own skeleton - the visor rotator on a headset, a gimbal on a drone.
	OWNER,
	// Only the carrier's skeleton - a head bone on the character wearing this.
	CARRIER,
}

//------------------------------------------------------------------------------------------------
// How the camera's resting orientation is decided.
//------------------------------------------------------------------------------------------------
enum ACM_ERemoteFeedAimMode
{
	// Camera rides the anchor. Turning the object turns the view - a fixed cam bolted to a dog.
	FOLLOW_ANCHOR,
	// Camera keeps world-aligned angles regardless of how the object is oriented - a gimbal.
	WORLD_STABILISED,
}

//------------------------------------------------------------------------------------------------
// Where the wearer's display sits on screen. The renderer is told rather than asking, for the same
// reason it is handed a transform rather than an entity.
//
// Shape is expressed here rather than reused from the target detection types so this module stays
// self-contained. The device does the translation.
//------------------------------------------------------------------------------------------------
enum ACM_ERemoteFeedRegionShape
{
	// No display region declared. The feed falls back to a fixed size in a screen corner.
	FULLSCREEN,
	// Tube optic. extentX is the radius as a fraction of screen HEIGHT, on both axes, so it stays
	// round on a wide monitor instead of stretching into an ellipse.
	CIRCLE,
	// Flat visor. extentX and extentY are half-extents as fractions of screen width and height.
	RECTANGLE,
}

//------------------------------------------------------------------------------------------------
class ACM_RemoteFeedDisplayRegion
{
	ACM_ERemoteFeedRegionShape shape;

	// Fractions of the screen, never pixels, so a resolution change needs no recomputation.
	float centerX = 0.5;
	float centerY = 0.5;
	float extentX = 0.42;
	float extentY = 0.42;

	//------------------------------------------------------------------------------------------------
	void Set(ACM_ERemoteFeedRegionShape regionShape, float cx, float cy, float ex, float ey)
	{
		shape = regionShape;
		centerX = cx;
		centerY = cy;
		extentX = ex;
		extentY = ey;
	}

	//------------------------------------------------------------------------------------------------
	// Largest axis-aligned rectangle inside the region, in screen pixels. For a circle that is the
	// inscribed square - the bounding box would hang the panel's corners outside the tube.
	//------------------------------------------------------------------------------------------------
	void GetUsableRect(float screenW, float screenH, out float outX, out float outY, out float outW, out float outH)
	{
		float cx = screenW * centerX;
		float cy = screenH * centerY;

		float halfW = screenW * 0.5;
		float halfH = screenH * 0.5;

		if (shape == ACM_ERemoteFeedRegionShape.CIRCLE)
		{
			float radius = screenH * extentX;
			float inscribed = radius * 0.70710678;
			halfW = inscribed;
			halfH = inscribed;
		}
		else if (shape == ACM_ERemoteFeedRegionShape.RECTANGLE)
		{
			halfW = screenW * extentX;
			halfH = screenH * extentY;
		}

		outX = cx - halfW;
		outY = cy - halfH;
		outW = halfW * 2;
		outH = halfH * 2;
	}
}

//------------------------------------------------------------------------------------------------
// Distinct from quality: quality is a number, this is what the renderer is doing about it.
//
// LOST and RECONNECTING exist because a single threshold makes a feed sat on the cutoff flicker
// several times a second. Entering and leaving are separate decisions.
//------------------------------------------------------------------------------------------------
enum ACM_ERemoteFeedLinkState
{
	// No source selected, or the source is not authorised. Nothing is evaluated.
	NONE,
	// Rendering, at whatever fidelity the quality band allows.
	LIVE,
	// Below the loss threshold and holding: last frame held, render updates suspended.
	LOST,
	// Quality has recovered and is holding, but the feed has not resumed full rate yet.
	RECONNECTING,
}

//------------------------------------------------------------------------------------------------
// The renderer owns the numbers, not the decision about when a band applies.
//------------------------------------------------------------------------------------------------
class ACM_RemoteFeedBand
{
	float minQuality;
	int maxFPS;
	float resolutionScale;
	// 0 clean, 1 unwatchable. Drives grain intensity and jitter amplitude.
	float interference;
	string label;

	//------------------------------------------------------------------------------------------------
	void ACM_RemoteFeedBand(float quality, int fps, float scale, float noise, string text)
	{
		minQuality = quality;
		maxFPS = fps;
		resolutionScale = scale;
		interference = noise;
		label = text;
	}
}
