//------------------------------------------------------------------------------------------------
// ACM NVG TUBE VIEW
//
// Shows night vision ONLY inside the tube circles; everything outside them is the normal,
// untouched view of the world.
//
// How it works:
//   - The main camera is left completely alone (no HDR tint, no grain, no vignette).
//   - A second world camera (CAMERA_INDEX) copies the main camera's transform, FOV and near
//     plane every frame, and is the camera the night vision post-process chain is applied to.
//   - That camera renders into a full-screen RTTextureWidget.
//   - One ImageWidget per tube shows the matching screen region of that render (custom UVs),
//     cut to a soft circle by an alpha mask. Because each tube samples exactly the part of the
//     render that sits behind it, the night vision picture lines up with the world around it.
//
// Only ONE view exists per client, and only for the local player's active device. The previous
// per-entity overlay created a camera and a full-screen widget for every PVS-15 in the world,
// all fighting over the same camera index.
//
// Geometry is in SCREEN HEIGHTS measured from the screen centre, so circles stay round on any
// aspect ratio (16:9, ultrawide, 4:3). Overlapping tubes are cut at straight vertical seams.
//------------------------------------------------------------------------------------------------

enum ACM_ENVGTubeLayout
{
	FULLSCREEN,	//!< Legacy behaviour: the effect covers the whole screen.
	SINGLE,		//!< One centred tube (PVS-14, PVS-7).
	DUAL,		//!< Two side-by-side tubes (PVS-15, PVS-31, PVS-5).
	PANORAMIC,	//!< Three overlapping tubes in a row (GPNVG / FPGPNVG).
	CUSTOM		//!< Whatever is listed in the device's custom tube array.
}

[BaseContainerProps()]
class ACM_NVGTube
{
	[Attribute("0", UIWidgets.EditBox, "Horizontal centre in screen heights from the screen centre. Negative is left.")]
	float m_fCenterX;

	[Attribute("0", UIWidgets.EditBox, "Vertical centre in screen heights from the screen centre. Negative is up.")]
	float m_fCenterY;

	[Attribute("0.4", UIWidgets.EditBox, "Radius in screen heights. 0.5 is a circle as tall as the screen.")]
	float m_fRadius;

	//------------------------------------------------------------------------------------------------
	static ACM_NVGTube Make(float x, float y, float radius)
	{
		ACM_NVGTube tube = new ACM_NVGTube();
		tube.m_fCenterX = x;
		tube.m_fCenterY = y;
		tube.m_fRadius = radius;
		return tube;
	}
}

class ACM_NVGTubeViewClass : GenericEntityClass
{
}

class ACM_NVGTubeView : GenericEntity
{
	//! World camera index used for the night vision render. Vanilla PIP uses 8, the remote feed 11-15.
	static const int CAMERA_INDEX = 20;

	protected static const ResourceName LAYOUT = "{E3F1A9B2C4D74A62}UI/Layouts/ACM_NVG_DualTubeOverlay.layout";
	//! Alpha = distance from the centre (0 centre, 1 rim). Lets softness be tuned at runtime.
	protected static const ResourceName MASK_RADIAL = "{7688B9B44C3F0674}Textures/ACM_NVG_TubeMaskSoft.edds";
	//! Older hard-edged circle (alpha 1 inside). Used only if the radial mask is not imported yet.
	protected static const ResourceName MASK_FALLBACK = "{D9407BBA0E4848A7}Textures/ACM_NVG_TubeMask.edds";
	//! Thin black ring that fades in towards the tube rim. Alpha = 1 - ring opacity, so it is used
	//! with progress 0 / width 1 and the baked profile comes through unchanged.
	protected static const ResourceName MASK_RIM = "{224C67CE834FCE77}Textures/ACM_NVG_TubeOutline.edds";

	protected static ACM_NVGTubeView s_Instance;

	protected Managed m_Owner;
	protected ref array<ref ACM_NVGTube> m_aTubes = {};
	protected float m_fSoftness = 0.08;
	protected float m_fResolutionScale = 0.85;

	protected Widget m_wRoot;
	protected RTTextureWidget m_wRenderTexture;
	protected RenderTargetWidget m_wRenderTarget;
	protected Widget m_wImages;
	protected ref array<ImageWidget> m_aTubeWidgets = {};
	//! Per widget: which tube it belongs to, and how much wider than the tube it samples (edge distortion).
	protected ref array<int> m_aLayerTube = {};
	protected ref array<float> m_aLayerScale = {};
	protected float m_fDistortion = 0.12;

	//! Faded black outline drawn over each tube, and the tube each one belongs to.
	protected ref array<ImageWidget> m_aRimWidgets = {};
	protected ref array<int> m_aRimTube = {};
	//! One clipping frame per tube (same index as m_aTubes), cut at the seams with its neighbours.
	protected ref array<Widget> m_aClipFrames = {};
	protected float m_fRimStrength = 1.0;
	protected static bool s_bRimMaskLogged;
	//! Edge distortion: stacked copies of each tube, outermost first. Each ring shows a slightly
	//! larger area of the render than the one inside it, so the picture compresses towards the rim.
	protected static const int DISTORTION_LAYERS = 4;
	protected static const float DISTORTION_START = 0.75;

	protected bool m_bReady;
	protected bool m_bRenderTargetSet;
	//! Frames since the widgets were (re)built. The render texture is allocated lazily by the
	//! engine, so the tube images are re-linked to it a few frames later.
	protected int m_iFramesSinceBuild;
	protected int m_iLayoutWidth = -1;
	protected int m_iLayoutHeight = -1;
	protected float m_fAppliedFOV = -1;
	protected float m_fAppliedNear = -1;
	protected float m_fAppliedFar = -1;

	//! 0 = not checked yet, 1 = radial mask available, 2 = missing (hard-edged fallback).
	protected static int s_iRadialMaskState;

	//------------------------------------------------------------------------------------------------
	// FUSED THERMAL (F-Pano): a second camera renders thermal; it is drawn in a smaller circle inside
	// the centre tube, blended additively over the night vision. The thermal palette is black except
	// for the hottest temperatures, so only warm things show up on top of the night vision picture.
	//------------------------------------------------------------------------------------------------
	//! World camera index used for the fused thermal render.
	static const int FUSION_CAMERA_INDEX = 21;
	protected static const int FUSION_PP_THERMAL_SLOT = 11;
	protected static const int FUSION_PP_HDR_SLOT = 16;
	protected static const ResourceName FUSION_DEFAULT_HDR = "{9DEECCABE8357209}Common/Postprocess/HDR.emat";
	//! Full resolution: the overlay shows thin outlines, which go soft at lower resolutions.
	protected static const float FUSION_RESOLUTION_SCALE = 1.0;

	protected RTTextureWidget m_wFusionTexture;
	protected RenderTargetWidget m_wFusionTarget;
	protected ImageWidget m_wFusionImage;
	protected bool m_bFusionOn;
	protected bool m_bFusionTargetSet;
	protected ResourceName m_sFusionMaterial;
	//! Overlay circle size (fraction of the centre tube) and opacity.
	protected float m_fFusionRadius = 0.8;
	protected float m_fFusionOpacity = 0.5;
	//! Blend of the overlay image (widget flags are fixed at creation, so a change rebuilds it).
	protected bool m_bFusionAdditive = true;
	protected static bool s_bFusionMissingLogged;

	//------------------------------------------------------------------------------------------------
	// PUBLIC API
	//------------------------------------------------------------------------------------------------
	//! Show (or re-shape) the tube view for this owner. Returns null if it could not be built, in
	//! which case the caller should fall back to a full-screen effect on the main camera.
	static ACM_NVGTubeView Acquire(Managed owner, ACM_ENVGTubeLayout layout, array<ref ACM_NVGTube> customTubes, float softness, float resolutionScale, float edgeDistortion = 0.12, float rimStrength = 1.0)
	{
		if (!owner || layout == ACM_ENVGTubeLayout.FULLSCREEN)
			return null;

		if (!GetGame() || !GetGame().GetWorld() || !GetGame().GetWorkspace())
			return null;

		array<ref ACM_NVGTube> tubes = {};
		BuildTubes(layout, customTubes, tubes);
		if (tubes.IsEmpty())
			return null;

		if (!s_Instance)
		{
			s_Instance = ACM_NVGTubeView.Cast(GetGame().SpawnEntity(ACM_NVGTubeView, GetGame().GetWorld()));
			if (!s_Instance)
				return null;

			if (!s_Instance.Build(resolutionScale))
			{
				delete s_Instance;
				s_Instance = null;
				return null;
			}
		}

		s_Instance.Configure(owner, tubes, softness, resolutionScale, edgeDistortion, rimStrength);
		return s_Instance;
	}

	//------------------------------------------------------------------------------------------------
	//! Remove the view, but only if this owner is the one currently showing it.
	static void Release(Managed owner)
	{
		if (!s_Instance || !owner || s_Instance.m_Owner != owner)
			return;

		// Tear down while the entity is still alive so the render texture can be unregistered.
		s_Instance.Teardown();
		delete s_Instance;
		s_Instance = null;
	}

	//------------------------------------------------------------------------------------------------
	//! Called from the local player's camera handler right after the camera transform for this frame
	//! is computed - the only point where the current view is known before rendering.
	static void OnPlayerCameraUpdated(IEntity cameraOwner, vector transformWS[4])
	{
		if (!s_Instance || !s_Instance.m_bReady || !cameraOwner)
			return;

		if (cameraOwner != SCR_PlayerController.GetLocalControlledEntity())
			return;

		s_Instance.ApplyCameraTransform(transformWS);
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyCameraTransform(vector transformWS[4])
	{
		BaseWorld world = GetWorld();
		if (!world)
			return;

		world.SetCameraEx(CAMERA_INDEX, transformWS);
		if (m_bFusionOn)
			world.SetCameraEx(FUSION_CAMERA_INDEX, transformWS);
		m_fHookWorldTime = world.GetWorldTime();

		if (!m_bHookSeen)
		{
			m_bHookSeen = true;
			Print("[ACM NVG] Tube camera is now fed directly by the player camera.", LogLevel.NORMAL);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! True while a tube view (from any device) is up.
	static bool IsUp()
	{
		return s_Instance && s_Instance.m_bReady;
	}

	//------------------------------------------------------------------------------------------------
	//! Turn the fused thermal overlay (F-Pano) on or off in the centre tube of the current view.
	//! Returns false if there is no tube view or the layout lacks the fusion render target.
	//! additive: true adds the thermal on top (black adds nothing - outline mode); false blends it
	//! over the night vision at the given opacity (fused / full thermal modes).
	static bool SetFusionOverlay(bool enabled, ResourceName thermalMaterial, float radiusFraction = 0.8, float opacity = 0.5, bool additive = true)
	{
		if (!s_Instance || !s_Instance.m_bReady)
			return false;

		return s_Instance.ApplyFusion(enabled, thermalMaterial, radiusFraction, opacity, additive);
	}

	//------------------------------------------------------------------------------------------------
	static bool IsFusionOn()
	{
		return s_Instance && s_Instance.m_bFusionOn;
	}

	//------------------------------------------------------------------------------------------------
	static bool IsActiveFor(Managed owner)
	{
		return s_Instance && owner && s_Instance.m_Owner == owner && s_Instance.m_bReady;
	}
	//------------------------------------------------------------------------------------------------
	//! Tube presets, in screen heights from the screen centre.
	static void BuildTubes(ACM_ENVGTubeLayout layout, array<ref ACM_NVGTube> customTubes, notnull array<ref ACM_NVGTube> outTubes)
	{
		outTubes.Clear();

		switch (layout)
		{
			case ACM_ENVGTubeLayout.SINGLE:
				outTubes.Insert(ACM_NVGTube.Make(0, -0.02, 0.44));
				break;

			case ACM_ENVGTubeLayout.DUAL:
				outTubes.Insert(ACM_NVGTube.Make(-0.30, 0, 0.38));
				outTubes.Insert(ACM_NVGTube.Make(0.30, 0, 0.38));
				break;

			case ACM_ENVGTubeLayout.PANORAMIC:
				// Three equal tubes on one horizontal line, slightly overlapping. Where they overlap
				// they are cut at straight vertical seams (GetClipBounds).
				outTubes.Insert(ACM_NVGTube.Make(-0.52, 0, 0.31));
				outTubes.Insert(ACM_NVGTube.Make(0, 0, 0.31));
				outTubes.Insert(ACM_NVGTube.Make(0.52, 0, 0.31));
				break;

			case ACM_ENVGTubeLayout.CUSTOM:
				if (customTubes)
				{
					foreach (ACM_NVGTube tube : customTubes)
					{
						if (tube && tube.m_fRadius > 0)
							outTubes.Insert(ACM_NVGTube.Make(tube.m_fCenterX, tube.m_fCenterY, tube.m_fRadius));
					}
				}
				break;
		}
	}

	//------------------------------------------------------------------------------------------------
	// LIFETIME
	//------------------------------------------------------------------------------------------------
	void ACM_NVGTubeView(IEntitySource src, IEntity parent)
	{
		SetFlags(EntityFlags.ACTIVE, false);
		SetEventMask(EntityEvent.POSTFRAME);
	}

	//------------------------------------------------------------------------------------------------
	void ~ACM_NVGTubeView()
	{
		Teardown(true);
		if (s_Instance == this)
			s_Instance = null;
	}

	//------------------------------------------------------------------------------------------------
	protected bool Build(float resolutionScale)
	{
		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if (!workspace)
			return Fail("no UI workspace");

		m_wRoot = workspace.CreateWidgets(LAYOUT);
		if (!m_wRoot)
			return Fail("layout failed to load");

		m_wRenderTexture = RTTextureWidget.Cast(m_wRoot.FindAnyWidget("ACMTubeRT"));
		m_wRenderTarget = RenderTargetWidget.Cast(m_wRoot.FindAnyWidget("ACMTubeTarget"));
		m_wImages = m_wRoot.FindAnyWidget("ACMTubeImages");
		// Optional: fused thermal render target (F-Pano). Idle until SetFusionOverlay turns it on.
		m_wFusionTexture = RTTextureWidget.Cast(m_wRoot.FindAnyWidget("ACMFusionRT"));
		m_wFusionTarget = RenderTargetWidget.Cast(m_wRoot.FindAnyWidget("ACMFusionTarget"));
		if (!m_wRenderTexture || !m_wRenderTarget || !m_wImages)
			return Fail("layout is missing ACMTubeRT / ACMTubeTarget / ACMTubeImages");

		// Below the HUD, above the 3D scene.
		m_wRoot.SetZOrder(-100);

		BaseWorld world = GetWorld();
		world.SetCameraType(CAMERA_INDEX, CameraType.PERSPECTIVE);
		world.SetCameraFarPlane(CAMERA_INDEX, GetGame().GetViewDistance());
		SyncCamera();

		m_wRenderTarget.SetWorld(world, CAMERA_INDEX);
		m_wRenderTarget.SetRefresh(1, 0);
		m_wRenderTarget.SetResolutionScale(resolutionScale, resolutionScale);
		m_fResolutionScale = resolutionScale;

		// Vanilla PIP always registers the render texture with an entity before it is used; do the
		// same so the engine creates and updates it.
		m_wRenderTexture.SetRenderTarget(this);
		m_bRenderTargetSet = true;

		m_bReady = true;
		Print("[ACM NVG] Tube view created on camera " + CAMERA_INDEX.ToString() + " at resolution scale " + resolutionScale.ToString(4, 2) + ".", LogLevel.NORMAL);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected bool Fail(string reason)
	{
		Print("[ACM NVG] Tube view unavailable, using full-screen effect instead: " + reason, LogLevel.ERROR);
		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected void Configure(Managed owner, notnull array<ref ACM_NVGTube> tubes, float softness, float resolutionScale, float edgeDistortion, float rimStrength)
	{
		m_Owner = owner;

		if (resolutionScale != m_fResolutionScale && m_wRenderTarget)
		{
			m_fResolutionScale = resolutionScale;
			m_wRenderTarget.SetResolutionScale(resolutionScale, resolutionScale);
		}

		float newSoftness = Math.Clamp(softness, 0.0, 0.6);
		float newDistortion = Math.Clamp(edgeDistortion, 0.0, 0.5);
		float newRim = Math.Clamp(rimStrength, 0.0, 1.0);
		if (newSoftness == m_fSoftness && newDistortion == m_fDistortion && newRim == m_fRimStrength && SameTubes(tubes) && !m_aTubeWidgets.IsEmpty())
			return;

		m_fSoftness = newSoftness;
		m_fDistortion = newDistortion;
		m_fRimStrength = newRim;
		m_aTubes.Clear();
		foreach (ACM_NVGTube tube : tubes)
		{
			m_aTubes.Insert(tube);
		}

		RebuildTubeWidgets();
	}

	//------------------------------------------------------------------------------------------------
	protected bool SameTubes(notnull array<ref ACM_NVGTube> tubes)
	{
		if (tubes.Count() != m_aTubes.Count())
			return false;

		for (int i = 0; i < tubes.Count(); i++)
		{
			ACM_NVGTube a = tubes[i];
			ACM_NVGTube b = m_aTubes[i];
			if (!a || !b || a.m_fCenterX != b.m_fCenterX || a.m_fCenterY != b.m_fCenterY || a.m_fRadius != b.m_fRadius)
				return false;
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected void Teardown(bool fromDestructor = false)
	{
		StopFusion(fromDestructor);
		m_bReady = false;
		m_aTubeWidgets.Clear();
		m_aLayerTube.Clear();
		m_aLayerScale.Clear();
		m_aRimWidgets.Clear();
		m_aRimTube.Clear();
		m_aClipFrames.Clear();

		// Only while the entity is still alive; from the destructor the engine has already deleted it
		// and drops the registration itself.
		if (m_bRenderTargetSet && m_wRenderTexture && !fromDestructor)
			m_wRenderTexture.RemoveRenderTarget(this);
		m_bRenderTargetSet = false;

		if (m_wRoot)
		{
			m_wRoot.RemoveFromHierarchy();
			m_wRoot = null;
		}

		m_wRenderTexture = null;
		m_wRenderTarget = null;
		m_wImages = null;
		m_wFusionTexture = null;
		m_wFusionTarget = null;
		m_wFusionImage = null;
	}

	//------------------------------------------------------------------------------------------------
	// TUBES
	//------------------------------------------------------------------------------------------------
	protected void RebuildTubeWidgets()
	{
		// Removing a tube's clip frame removes its picture layers and outline with it.
		foreach (Widget oldClip : m_aClipFrames)
		{
			if (oldClip)
				oldClip.RemoveFromHierarchy();
		}
		m_aClipFrames.Clear();
		m_aTubeWidgets.Clear();
		m_aLayerTube.Clear();
		m_aLayerScale.Clear();
		m_aRimWidgets.Clear();
		m_aRimTube.Clear();

		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if (!workspace || !m_wImages || !m_wRenderTexture)
			return;

		// Edge distortion needs the soft radial mask to blend its rings together.
		bool radial = HasRadialMask(workspace);
		int layers = 1;
		if (radial && m_fDistortion > 0)
			layers = DISTORTION_LAYERS;

		for (int tubeIndex = 0; tubeIndex < m_aTubes.Count(); tubeIndex++)
		{
			// Each tube lives in its own clipping frame, cut at the straight seams with its
			// neighbours (see UpdateTubeLayout), so overlapping tubes meet on a vertical line.
			Widget clip = workspace.CreateWidget(WidgetType.FrameWidgetTypeID, WidgetFlags.VISIBLE | WidgetFlags.CLIPCHILDREN | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS, Color.White, 0, m_wImages);
			if (!clip)
				continue;

			FrameSlot.SetAnchorMin(clip, 0, 0);
			FrameSlot.SetAnchorMax(clip, 0, 0);
			FrameSlot.SetAlignment(clip, 0, 0);
			m_aClipFrames.Insert(clip);

			// Outermost ring first so the rings inside it draw on top.
			for (int layer = 0; layer < layers; layer++)
			{
				ImageWidget image = CreateTubeImage(workspace, clip);
				if (!image)
					continue;

				float outerRadius = 1.0;
				float scale = 1.0;
				if (layers > 1)
				{
					outerRadius = LayerRadius(layer);
					if (layer < layers - 1)
					{
						float innerRadius = LayerRadius(layer + 1);
						float t = ((outerRadius + innerRadius) * 0.5 - DISTORTION_START) / (1.0 - DISTORTION_START);
						scale = 1.0 + m_fDistortion * t * t;
					}
				}

				ApplyMask(image, radial, outerRadius, layer == 0);

				m_aTubeWidgets.Insert(image);
				m_aLayerTube.Insert(tubeIndex);
				m_aLayerScale.Insert(scale);
			}

			if (m_fRimStrength > 0)
				CreateRim(workspace, tubeIndex, clip);
		}

		// Force a reposition on the next frame.
		m_iLayoutWidth = -1;
		m_iLayoutHeight = -1;
		m_iFramesSinceBuild = 0;
		UpdateTubeLayout();

		// The fused thermal circle lived in a clip frame that was just replaced.
		m_wFusionImage = null;
		if (m_bFusionOn)
			CreateFusionImage();
	}

	//------------------------------------------------------------------------------------------------
	//! Faded black outline: a plain black image cut to a ring by the rim mask.
	protected void CreateRim(WorkspaceWidget workspace, int tubeIndex, Widget parent)
	{
		// Missing texture logs a GUI error on every load attempt, so stop after the first failure.
		if (s_bRimMaskLogged)
			return;

		ImageWidget rim = ImageWidget.Cast(workspace.CreateWidget(WidgetType.ImageWidgetTypeID, WidgetFlags.VISIBLE | WidgetFlags.BLEND | WidgetFlags.STRETCH | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS, Color.Black, 0, parent));
		if (!rim)
			return;

		if (!rim.LoadMaskTexture(MASK_RIM))
		{
			if (!s_bRimMaskLogged)
			{
				s_bRimMaskLogged = true;
				Print("[ACM NVG] Tube outline mask not found (Textures/ACM_NVG_TubeOutline.edds) - tubes are drawn without the black outline.", LogLevel.WARNING);
			}

			rim.RemoveFromHierarchy();
			return;
		}

		rim.SetMaskMode(ImageMaskMode.REGULAR);
		rim.SetMaskProgress(0);
		rim.SetMaskTransitionWidth(1);
		rim.SetOpacity(m_fRimStrength);

		FrameSlot.SetAnchorMin(rim, 0, 0);
		FrameSlot.SetAnchorMax(rim, 0, 0);
		FrameSlot.SetAlignment(rim, 0, 0);

		m_aRimWidgets.Insert(rim);
		m_aRimTube.Insert(tubeIndex);
	}

	//------------------------------------------------------------------------------------------------
	//! Points every tube image at the render texture again (after it has been allocated / resized).
	protected void RelinkRenderTexture()
	{
		if (!m_wRenderTexture)
			return;

		foreach (ImageWidget image : m_aTubeWidgets)
		{
			if (!image)
				continue;

			image.SetImageTexture(0, m_wRenderTexture);
			image.SetImage(0);
		}

		if (m_bFusionOn && m_wFusionImage && m_wFusionTexture)
		{
			m_wFusionImage.SetImageTexture(0, m_wFusionTexture);
			m_wFusionImage.SetImage(0);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Outer edge of each distortion ring, as a fraction of the tube radius. The last one is the
	//! undistorted centre.
	protected float LayerRadius(int layer)
	{
		switch (layer)
		{
			case 0: return 1.0;
			case 1: return 0.93;
			case 2: return 0.85;
		}

		return DISTORTION_START;
	}

	//------------------------------------------------------------------------------------------------
	protected ImageWidget CreateTubeImage(WorkspaceWidget workspace, Widget parent)
	{
		ImageWidget image = ImageWidget.Cast(workspace.CreateWidget(WidgetType.ImageWidgetTypeID, WidgetFlags.VISIBLE | WidgetFlags.BLEND | WidgetFlags.STRETCH | WidgetFlags.CUSTOMUV | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS, Color.White, 0, parent));
		if (!image)
			return null;

		image.SetImageTexture(0, m_wRenderTexture);
		image.SetImage(0);

		FrameSlot.SetAnchorMin(image, 0, 0);
		FrameSlot.SetAnchorMax(image, 0, 0);
		FrameSlot.SetAlignment(image, 0, 0);
		return image;
	}

	//------------------------------------------------------------------------------------------------
	//! Checks once per session whether the radial mask texture is imported. A missing texture logs
	//! a GUI error on every load attempt, so it is never retried.
	protected bool HasRadialMask(WorkspaceWidget workspace)
	{
		if (s_iRadialMaskState == 0)
		{
			s_iRadialMaskState = 2;
			ImageWidget probe = ImageWidget.Cast(workspace.CreateWidget(WidgetType.ImageWidgetTypeID, WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS, Color.White, 0, m_wImages));
			if (probe)
			{
				if (probe.LoadMaskTexture(MASK_RADIAL))
					s_iRadialMaskState = 1;
				probe.RemoveFromHierarchy();
			}

			if (s_iRadialMaskState == 2)
				Print("[ACM NVG] Soft tube mask not found (Textures/ACM_NVG_TubeMaskSoft.edds) - using the hard-edged mask without edge distortion.", LogLevel.WARNING);
		}

		return s_iRadialMaskState == 1;
	}

	//------------------------------------------------------------------------------------------------
	//! Mask alpha below progress is opaque, above (progress + width) transparent. The radial mask's
	//! alpha is the distance from the tube centre (0 centre, 1 rim).
	protected void ApplyMask(ImageWidget image, bool radial, float outerRadius, bool isOutermost)
	{
		if (radial && image.LoadMaskTexture(MASK_RADIAL))
		{
			image.SetMaskMode(ImageMaskMode.REGULAR);

			float width;
			float progress;
			if (isOutermost)
			{
				// Tube rim: fades out over the last m_fSoftness of the radius.
				width = Math.Max(m_fSoftness, 0.005);
				progress = Math.Min(outerRadius, 0.98) - width;
			}
			else
			{
				// Ring boundary inside the tube: a short blend into the ring underneath.
				width = 0.04;
				progress = outerRadius - width * 0.5;
			}

			image.SetMaskProgress(Math.Clamp(progress, 0.0, 1.0));
			image.SetMaskTransitionWidth(width);
			return;
		}

		if (image.LoadMaskTexture(MASK_FALLBACK))
		{
			image.SetMaskMode(ImageMaskMode.INVERTED);
			image.SetMaskProgress(0.5);
			image.SetMaskTransitionWidth(0.1);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Positions each tube and points its UVs at the matching region of the render.
	//! Only does work when the screen size changes.
	protected void UpdateTubeLayout()
	{
		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if (!workspace)
			return;

		int pixelWidth = workspace.GetWidth();
		int pixelHeight = workspace.GetHeight();
		if (pixelWidth <= 0 || pixelHeight <= 0)
			return;

		if (pixelWidth == m_iLayoutWidth && pixelHeight == m_iLayoutHeight)
			return;

		m_iLayoutWidth = pixelWidth;
		m_iLayoutHeight = pixelHeight;

		// Screen size changed: the fused circle follows the centre tube.
		PositionFusionImage();

		// A resize re-creates the render texture, so re-link the images over the next frames.
		m_iFramesSinceBuild = 0;

		float screenW = workspace.DPIUnscale(pixelWidth);
		float screenH = workspace.DPIUnscale(pixelHeight);

		// Clip frames: each tube's box, cut at the straight seams with its neighbours.
		for (int c = 0; c < m_aClipFrames.Count(); c++)
		{
			Widget clip = m_aClipFrames[c];
			if (!clip || !m_aTubes.IsIndexValid(c) || !m_aTubes[c])
				continue;

			ACM_NVGTube clipTube = m_aTubes[c];
			float clipLo, clipHi;
			GetClipBounds(c, clipLo, clipHi);

			FrameSlot.SetPos(clip, screenW * 0.5 + clipLo * screenH, screenH * 0.5 + (clipTube.m_fCenterY - clipTube.m_fRadius) * screenH);
			FrameSlot.SetSize(clip, (clipHi - clipLo) * screenH, clipTube.m_fRadius * 2 * screenH);
		}

		for (int i = 0; i < m_aTubeWidgets.Count(); i++)
		{
			ImageWidget image = m_aTubeWidgets[i];
			int tubeIndex = m_aLayerTube[i];
			if (!image || !m_aTubes.IsIndexValid(tubeIndex))
				continue;

			ACM_NVGTube tube = m_aTubes[tubeIndex];
			if (!tube)
				continue;

			float size = tube.m_fRadius * 2 * screenH;
			float left = screenW * 0.5 + (tube.m_fCenterX - tube.m_fRadius) * screenH;
			float top = screenH * 0.5 + (tube.m_fCenterY - tube.m_fRadius) * screenH;

			// Positioned inside the tube's clip frame, so relative to the frame's left edge.
			float tubeLo, tubeHi;
			GetClipBounds(tubeIndex, tubeLo, tubeHi);
			FrameSlot.SetPos(image, (tube.m_fCenterX - tube.m_fRadius - tubeLo) * screenH, 0);
			FrameSlot.SetSize(image, size, size);

			// Sample the region behind the tube; distortion rings sample a slightly larger area
			// around the same centre, which squeezes the picture towards the rim.
			float scale = m_aLayerScale[i];
			float centerU = (left + size * 0.5) / screenW;
			float centerV = (top + size * 0.5) / screenH;
			float halfU = size * 0.5 * scale / screenW;
			float halfV = size * 0.5 * scale / screenH;

			float u0 = centerU - halfU;
			float v0 = centerV - halfV;
			float u1 = centerU + halfU;
			float v1 = centerV + halfV;

			// Corner order: top-left, top-right, bottom-right, bottom-left.
			float uv[4][2];
			uv[0][0] = u0;	uv[0][1] = v0;
			uv[1][0] = u1;	uv[1][1] = v0;
			uv[2][0] = u1;	uv[2][1] = v1;
			uv[3][0] = u0;	uv[3][1] = v1;
			image.SetUV(uv);
		}

		for (int r = 0; r < m_aRimWidgets.Count(); r++)
		{
			ImageWidget rim = m_aRimWidgets[r];
			int rimTube = m_aRimTube[r];
			if (!rim || !m_aTubes.IsIndexValid(rimTube))
				continue;

			ACM_NVGTube rimSource = m_aTubes[rimTube];
			if (!rimSource)
				continue;

			float rimSize = rimSource.m_fRadius * 2 * screenH;
			float rimLo, rimHi;
			GetClipBounds(rimTube, rimLo, rimHi);
			FrameSlot.SetPos(rim, (rimSource.m_fCenterX - rimSource.m_fRadius - rimLo) * screenH, 0);
			FrameSlot.SetSize(rim, rimSize, rimSize);
		}	}

	//------------------------------------------------------------------------------------------------
	//! Horizontal extent of a tube after cutting it at the seams with overlapping neighbours, in
	//! screen heights from the screen centre. The seam is the straight line through the two points
	//! where the circles cross (vertical when the tubes sit on one horizontal line).
	protected void GetClipBounds(int index, out float lo, out float hi)
	{
		ACM_NVGTube tube = m_aTubes[index];
		lo = tube.m_fCenterX - tube.m_fRadius;
		hi = tube.m_fCenterX + tube.m_fRadius;

		for (int j = 0; j < m_aTubes.Count(); j++)
		{
			ACM_NVGTube other = m_aTubes[j];
			if (j == index || !other)
				continue;

			float dx = tube.m_fCenterX - other.m_fCenterX;
			if (Math.AbsFloat(dx) < 0.0001 || Math.AbsFloat(dx) >= tube.m_fRadius + other.m_fRadius)
				continue;

			// Where both circles are equally "far in": the radical line.
			float seam = (tube.m_fCenterX * tube.m_fCenterX - other.m_fCenterX * other.m_fCenterX - tube.m_fRadius * tube.m_fRadius + other.m_fRadius * other.m_fRadius) / (2 * dx);

			if (other.m_fCenterX < tube.m_fCenterX)
				lo = Math.Max(lo, seam);
			else
				hi = Math.Min(hi, seam);
		}
	}

	//------------------------------------------------------------------------------------------------
	// CAMERA
	//------------------------------------------------------------------------------------------------
	//! World time of the last transform pushed by the player-camera hook (one value per frame).
	protected float m_fHookWorldTime = -1;
	protected bool m_bHookSeen;

	//! Last frame's raw world-camera read, used to predict the current frame (fallback path only).
	protected vector m_aPrevRead[4];
	protected bool m_bHavePrevRead;

	//------------------------------------------------------------------------------------------------
	//! No prediction across cuts (camera switch, teleport, very fast flick).
	protected bool CanPredict(vector prev[4], vector now[4])
	{
		if (vector.DistanceSq(prev[3], now[3]) > 4.0)
			return false;

		return vector.Dot(prev[2], now[2]) > 0.966; // under ~15 degrees per frame
	}

	//------------------------------------------------------------------------------------------------
	//! Keeps the tube camera's FOV / near plane in line with the main camera, and moves it when the
	//! player-camera hook did not (vehicle cameras, spectating, anything without that handler).
	protected void SyncCamera()
	{
		BaseWorld world = GetWorld();
		if (!world)
			return;

		float fov = 60;
		float nearPlane = 0.05;
		CameraBase mainCamera;
		CameraManager cameraManager = GetGame().GetCameraManager();
		if (cameraManager)
			mainCamera = cameraManager.CurrentCamera();

		if (mainCamera)
		{
			fov = mainCamera.GetVerticalFOV();
			nearPlane = mainCamera.GetNearPlane();
		}

		// Fallback when the player-camera hook does not drive the view. The player camera writes the
		// world camera in its own EOnPostFrame, which (measured) runs AFTER this one, so the world
		// camera here is last frame's view; continue last frame's motion one frame forward:
		// next = raw * inverse(prev) * raw (the same whichever multiplication order the engine uses).
		vector raw[4];
		world.GetCurrentCamera(raw);

		vector mat[4];
		Math3D.MatrixCopy(raw, mat);

		if (m_bHavePrevRead && CanPredict(m_aPrevRead, raw))
		{
			vector inv[4];
			vector step[4];
			Math3D.MatrixGetInverse4(m_aPrevRead, inv);
			Math3D.MatrixMultiply4(raw, inv, step);
			Math3D.MatrixMultiply4(step, raw, mat);
		}

		Math3D.MatrixCopy(raw, m_aPrevRead);
		m_bHavePrevRead = true;

		// The player-camera hook already set this frame's exact view; don't overwrite it with a guess.
		// (If the hook runs after this, its write wins anyway.)
		if (!m_bHookSeen || m_fHookWorldTime != world.GetWorldTime())
			world.SetCameraEx(CAMERA_INDEX, mat);

		if (fov != m_fAppliedFOV)
		{
			m_fAppliedFOV = fov;
			world.SetCameraVerticalFOV(CAMERA_INDEX, fov);
		}

		if (nearPlane != m_fAppliedNear && nearPlane > 0)
		{
			m_fAppliedNear = nearPlane;
			world.SetCameraNearPlane(CAMERA_INDEX, nearPlane);
		}

		// Follow the player's view distance setting if it changes while the view is up.
		float farPlane = GetGame().GetViewDistance();
		if (farPlane != m_fAppliedFar && farPlane > 0)
		{
			m_fAppliedFar = farPlane;
			world.SetCameraFarPlane(CAMERA_INDEX, farPlane);
		}

		// The fused thermal camera always matches the night vision camera exactly.
		if (m_bFusionOn)
		{
			if (!m_bHookSeen || m_fHookWorldTime != world.GetWorldTime())
				world.SetCameraEx(FUSION_CAMERA_INDEX, mat);

			world.SetCameraVerticalFOV(FUSION_CAMERA_INDEX, m_fAppliedFOV);
			if (m_fAppliedNear > 0)
				world.SetCameraNearPlane(FUSION_CAMERA_INDEX, m_fAppliedNear);
			if (m_fAppliedFar > 0)
				world.SetCameraFarPlane(FUSION_CAMERA_INDEX, m_fAppliedFar);
		}
	}

	//------------------------------------------------------------------------------------------------
	// FUSED THERMAL
	//------------------------------------------------------------------------------------------------
	protected bool ApplyFusion(bool enabled, ResourceName thermalMaterial, float radiusFraction, float opacity, bool additive = true)
	{
		// Blend change: the image has to be rebuilt with the other widget flags.
		if (additive != m_bFusionAdditive)
		{
			m_bFusionAdditive = additive;
			if (m_wFusionImage)
			{
				m_wFusionImage.RemoveFromHierarchy();
				m_wFusionImage = null;
			}
		}

		if (!enabled)
		{
			StopFusion(false);
			return true;
		}

		if (!m_wFusionTexture || !m_wFusionTarget)
		{
			if (!s_bFusionMissingLogged)
			{
				s_bFusionMissingLogged = true;
				Print("[ACM NVG] Fused thermal unavailable: the tube layout has no ACMFusionRT / ACMFusionTarget.", LogLevel.WARNING);
			}
			return false;
		}

		BaseWorld world = GetWorld();
		if (!world || thermalMaterial.IsEmpty())
		{
			Print("[ACM NVG] Fused thermal not started: " + "no world or no thermal material.", LogLevel.WARNING);
			return false;
		}

		m_fFusionRadius = Math.Clamp(radiusFraction, 0.1, 1.0);
		m_fFusionOpacity = Math.Clamp(opacity, 0.0, 1.0);

		if (!m_bFusionOn)
		{
			// Same view as the night vision camera.
			vector mat[4];
			world.GetCamera(CAMERA_INDEX, mat);
			world.SetCameraType(FUSION_CAMERA_INDEX, CameraType.PERSPECTIVE);
			world.SetCameraEx(FUSION_CAMERA_INDEX, mat);
			if (m_fAppliedFOV > 0)
				world.SetCameraVerticalFOV(FUSION_CAMERA_INDEX, m_fAppliedFOV);
			if (m_fAppliedNear > 0)
				world.SetCameraNearPlane(FUSION_CAMERA_INDEX, m_fAppliedNear);
			world.SetCameraFarPlane(FUSION_CAMERA_INDEX, GetGame().GetViewDistance());

			m_wFusionTarget.SetWorld(world, FUSION_CAMERA_INDEX);
			m_wFusionTarget.SetRefresh(1, 0);
			m_wFusionTarget.SetResolutionScale(FUSION_RESOLUTION_SCALE, FUSION_RESOLUTION_SCALE);

			if (!m_bFusionTargetSet)
			{
				m_wFusionTexture.SetRenderTarget(this);
				m_bFusionTargetSet = true;
			}

			m_bFusionOn = true;
		}

		if (thermalMaterial != m_sFusionMaterial)
		{
			m_sFusionMaterial = thermalMaterial;
			world.SetCameraPostProcessEffect(FUSION_CAMERA_INDEX, FUSION_PP_HDR_SLOT, PostProcessEffectType.HDR, FUSION_DEFAULT_HDR);
			world.SetCameraPostProcessEffect(FUSION_CAMERA_INDEX, FUSION_PP_THERMAL_SLOT, PostProcessEffectType.ThermalImaging, thermalMaterial);
		}

		if (!m_wFusionImage)
			CreateFusionImage();
		else
		{
			m_wFusionImage.SetOpacity(m_fFusionOpacity);
			PositionFusionImage();
		}

		Print("[ACM NVG] Fused thermal on (centre tube, " + (m_fFusionRadius * 100).ToString() + "% circle, " + (m_fFusionOpacity * 100).ToString() + "% opacity).", LogLevel.NORMAL);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected void StopFusion(bool fromDestructor)
	{
		if (!m_bFusionOn && !m_bFusionTargetSet)
			return;

		if (m_wFusionImage)
		{
			m_wFusionImage.RemoveFromHierarchy();
			m_wFusionImage = null;
		}

		BaseWorld world = GetWorld();
		if (world && !m_sFusionMaterial.IsEmpty())
		{
			world.SetCameraPostProcessEffect(FUSION_CAMERA_INDEX, FUSION_PP_THERMAL_SLOT, PostProcessEffectType.ThermalImaging, string.Empty);
			world.SetCameraPostProcessEffect(FUSION_CAMERA_INDEX, FUSION_PP_HDR_SLOT, PostProcessEffectType.HDR, FUSION_DEFAULT_HDR);
		}

		if (m_bFusionTargetSet && m_wFusionTexture && !fromDestructor)
			m_wFusionTexture.RemoveRenderTarget(this);

		m_bFusionTargetSet = false;
		m_bFusionOn = false;
		m_sFusionMaterial = string.Empty;
	}

	//------------------------------------------------------------------------------------------------
	//! The tube nearest the screen centre (the middle tube of a panoramic layout).
	protected int GetCenterTubeIndex()
	{
		int best = -1;
		float bestDistance = 1000000.0;
		for (int i = 0; i < m_aTubes.Count(); i++)
		{
			ACM_NVGTube tube = m_aTubes[i];
			if (!tube)
				continue;

			float distance = Math.AbsFloat(tube.m_fCenterX) + Math.AbsFloat(tube.m_fCenterY);
			if (distance < bestDistance)
			{
				bestDistance = distance;
				best = i;
			}
		}

		return best;
	}

	//------------------------------------------------------------------------------------------------
	//! The fused circle: an image of the thermal render, additive (black adds nothing), cut round by
	//! the soft radial mask, inside the centre tube's clip frame so it also respects the seams.
	protected void CreateFusionImage()
	{
		int center = GetCenterTubeIndex();
		if (center < 0 || !m_aClipFrames.IsIndexValid(center) || !m_aClipFrames[center] || !m_wFusionTexture)
			return;

		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if (!workspace)
			return;

		int flags = WidgetFlags.VISIBLE | WidgetFlags.BLEND | WidgetFlags.STRETCH | WidgetFlags.CUSTOMUV | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS;
		if (m_bFusionAdditive)
			flags |= WidgetFlags.ADDITIVE;

		ImageWidget image = ImageWidget.Cast(workspace.CreateWidget(WidgetType.ImageWidgetTypeID, flags, Color.White, 0, m_aClipFrames[center]));
		if (!image)
			return;

		image.SetImageTexture(0, m_wFusionTexture);
		image.SetImage(0);

		FrameSlot.SetAnchorMin(image, 0, 0);
		FrameSlot.SetAnchorMax(image, 0, 0);
		FrameSlot.SetAlignment(image, 0, 0);

		// Soft round edge (same radial mask as the tubes).
		ApplyMask(image, HasRadialMask(workspace), 1.0, true);
		image.SetOpacity(m_fFusionOpacity);

		m_wFusionImage = image;
		PositionFusionImage();
	}

	//------------------------------------------------------------------------------------------------
	//! Size / place the fused circle and point its UVs at the matching region of the thermal render.
	protected void PositionFusionImage()
	{
		if (!m_wFusionImage)
			return;

		WorkspaceWidget workspace = GetGame().GetWorkspace();
		int center = GetCenterTubeIndex();
		if (!workspace || center < 0 || !m_aTubes[center])
			return;

		float screenW = workspace.DPIUnscale(workspace.GetWidth());
		float screenH = workspace.DPIUnscale(workspace.GetHeight());
		if (screenW <= 0 || screenH <= 0)
			return;

		ACM_NVGTube tube = m_aTubes[center];
		float radius = tube.m_fRadius * m_fFusionRadius;
		float size = radius * 2 * screenH;

		float clipLo, clipHi;
		GetClipBounds(center, clipLo, clipHi);

		// Inside the clip frame: x relative to its left edge, y relative to the tube's top.
		FrameSlot.SetPos(m_wFusionImage, (tube.m_fCenterX - radius - clipLo) * screenH, (tube.m_fRadius - radius) * screenH);
		FrameSlot.SetSize(m_wFusionImage, size, size);

		float left = screenW * 0.5 + (tube.m_fCenterX - radius) * screenH;
		float top = screenH * 0.5 + (tube.m_fCenterY - radius) * screenH;

		float uv[4][2];
		uv[0][0] = left / screenW;			uv[0][1] = top / screenH;
		uv[1][0] = (left + size) / screenW;	uv[1][1] = top / screenH;
		uv[2][0] = (left + size) / screenW;	uv[2][1] = (top + size) / screenH;
		uv[3][0] = left / screenW;			uv[3][1] = (top + size) / screenH;
		m_wFusionImage.SetUV(uv);
	}

	//------------------------------------------------------------------------------------------------
	override protected void EOnPostFrame(IEntity owner, float timeSlice)
	{
		if (!m_bReady)
			return;

		SyncCamera();
		UpdateTubeLayout();

		// The engine only refreshes the render texture when an image links to it (it stayed live for
		// exactly the frames it was re-linked and froze afterwards), so link it every frame.
		RelinkRenderTexture();
	}
}
