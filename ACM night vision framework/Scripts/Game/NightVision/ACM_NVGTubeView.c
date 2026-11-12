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

	//! PERFORMANCE MODE. true: the night vision chain goes on the MAIN camera and this view only draws
	//! a black overlay with soft holes where the tubes are. Camera 20 no longer renders the world, which
	//! removes a whole extra scene render (NV + ECOTI drew the world three times: ~120 -> ~35 FPS).
	//! Camera 20 is still positioned every frame because the ECOTI thermal camera copies it.
	//! false: the old look (naked-eye view around the tubes, edge distortion) at the old cost.
	//! Set per device by Acquire (optimizedView); see m_bMainCameraNV.
	protected bool m_bMainCameraNV;

	//! m_bMainCameraNV + PERIPHERY_VIEW: the areas outside the tubes show the naked-eye view again (the
	//! original look), rendered by camera 20 as a CHEAP peripheral pass: low resolution, no night vision
	//! chain, short draw distance. The tubes themselves are the main camera at full quality.
	//! false: solid darkness outside the tubes (cheapest).
	static const bool PERIPHERY_VIEW = false;
	//! Render scale of the peripheral naked-eye view (it is dark, off-centre and seen in passing). The
	//! upscale from this low resolution is what softens / blurs the surroundings.
	protected static const float PERIPHERY_RESOLUTION_SCALE = 0.35;
	//! Surroundings exposure in light stops while the goggles are on (-2 = a quarter of the naked-eye
	//! brightness: dark around the tubes, still readable).
	protected static const float PERIPHERY_EV = -2.0;
	//! Gaussian blur on the surroundings (cheap: the periphery is rendered at low resolution).
	//! Strength is the material's Intensity (0-1).
	protected static const ResourceName PERIPHERY_BLUR = "{F51B0FE3E868ABDD}UI/Materials/ACM_PeripheryBlur.emat";
	protected static const int PERIPHERY_BLUR_SLOT = 7;
	//! Draw distance of the peripheral view in metres (the ECOTI camera keeps the full view distance).
	protected static const float PERIPHERY_FAR_PLANE = 1000;
	//! Naked-eye exposure for the peripheral camera (automatic, like the normal view).
	protected static const ResourceName PERIPHERY_HDR = "{9DEECCABE8357209}Common/Postprocess/HDR.emat";
	protected static const int PERIPHERY_HDR_SLOT = 16;

	//! Opacity of the darkness outside the tubes in m_bMainCameraNV mode without the periphery view.
	//! 1 = solid. Anything below lets the night vision picture underneath show through, and with it
	//! the phosphor colour, so the background changed colour with the goggles.
	protected static const float OUTSIDE_DARKNESS = 1.0;
	//! Black bands covering the screen outside the tubes' clip frames (m_bMainCameraNV mode).
	protected ref array<ImageWidget> m_aOutsideWidgets = {};

	//------------------------------------------------------------------------------------------------
	// RENDER CROP (performance, same look)
	//
	// The night vision and thermal cameras used to render the WHOLE screen although only the tubes
	// (night vision) and the centre circle (thermal) are ever shown. Each render target is now sized to
	// a box centred on the screen that just contains what is shown, and its camera's vertical FOV is
	// narrowed by the same ratio (the engine derives the horizontal FOV from the box's aspect), so the
	// picture lines up exactly as before. Fewer pixels AND a narrower view (fewer objects drawn).
	//------------------------------------------------------------------------------------------------
	protected static const bool CROP_RENDERS = false;
	//! Half extents of the crop boxes in screen heights from the screen centre (screen = 0.5 high).
	protected float m_fNVCropHalfW = -1;
	protected float m_fNVCropHalfH = 0.5;
	protected float m_fFusionCropHalfW = -1;
	protected float m_fFusionCropHalfH = 0.5;
	protected bool m_bCropFovDirty = true;
	//! Unscaled screen size the crops were computed for.
	protected float m_fCropScreenW = -1;
	protected float m_fCropScreenH = -1;

	//------------------------------------------------------------------------------------------------
	//! True when devices should put their effect chain on the main camera (see m_bMainCameraNV).
	static bool RendersOnMainCamera()
	{
		return s_Instance && s_Instance.m_bMainCameraNV;
	}

	//------------------------------------------------------------------------------------------------
	//! True when the areas outside the tubes show a rendered naked-eye view (not plain darkness).
	static bool ShowsPeriphery()
	{
		return s_Instance && s_Instance.m_bMainCameraNV && PERIPHERY_VIEW;
	}

	//------------------------------------------------------------------------------------------------
	//! The naked-eye exposure (main camera's automatic exposure before the night vision took it over).
	//! The periphery camera is LOCKED to it, darkened by PERIPHERY_EV, like vanilla PIP scopes copy the
	//! main camera's exposure. With its own automatic HDR it brightened a night scene to daylight.
	protected float m_fPeripheryExposure = -1;

	static void SetPeripheryExposure(float nakedEyeExposure)
	{
		if (!s_Instance || !ShowsPeriphery() || nakedEyeExposure <= 0)
			return;

		s_Instance.m_fPeripheryExposure = nakedEyeExposure;
		s_Instance.ApplyPeripheryExposure();
	}

	//------------------------------------------------------------------------------------------------
	protected void ApplyPeripheryExposure()
	{
		BaseWorld world = GetWorld();
		if (!world || !ShowsPeriphery())
			return;

		if (m_fPeripheryExposure > 0)
		{
			// Darkening baked into the locked exposure (2^EV), so it does not depend on how the engine
			// combines an EV offset with manual exposure.
			world.AdjustCameraEV(CAMERA_INDEX, 0);
			world.SetCameraHDRBrightness(CAMERA_INDEX, m_fPeripheryExposure * Math.Pow(2, PERIPHERY_EV));
			return;
		}

		// No reference yet: automatic exposure with the EV offset.
		world.SetCameraHDRBrightness(CAMERA_INDEX, -1);
		world.AdjustCameraEV(CAMERA_INDEX, PERIPHERY_EV);
	}

	//------------------------------------------------------------------------------------------------
	//! The camera a device using the tube view applies its post-process chain to.
	static int GetEffectCamera()
	{
		if (RendersOnMainCamera())
		{
			if (!GetGame() || !GetGame().GetWorld())
				return -1;
			return GetGame().GetWorld().GetCurrentCameraId();
		}

		return CAMERA_INDEX;
	}

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
	//! Fixed exposure of the thermal camera. Its automatic exposure kept re-metering the thermal picture
	//! (vanilla HDR.emat adapts almost instantly), so the whole thermal image pumped / flashed whenever
	//! the scene changed - worst in multiplayer with more moving heat. 1.0 passes the thermal palette
	//! through the tonemapper unchanged (same fix as the ACM Core / FlightCORE thermal sights).
	protected static const float FUSION_EXPOSURE = 1.0;
	protected static const ResourceName FUSION_DEFAULT_HDR = "{9DEECCABE8357209}Common/Postprocess/HDR.emat";
	//! Full resolution: the overlay shows thin outlines, which go soft at lower resolutions. (The
	//! outline material shrinks it to 60% itself; don't lower both or the outlines smear into streaks.)
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
	//! optimizedView: night vision on the MAIN camera with dark surroundings (no second world render);
	//! false: the separate tube camera with the naked-eye view around the tubes (F-Pano).
	static ACM_NVGTubeView Acquire(Managed owner, ACM_ENVGTubeLayout layout, array<ref ACM_NVGTube> customTubes, float softness, float resolutionScale, float edgeDistortion = 0.12, float rimStrength = 1.0, bool optimizedView = false)
	{
		if (!owner || layout == ACM_ENVGTubeLayout.FULLSCREEN)
			return null;

		if (!GetGame() || !GetGame().GetWorld() || !GetGame().GetWorkspace())
			return null;

		array<ref ACM_NVGTube> tubes = {};
		BuildTubes(layout, customTubes, tubes);
		if (tubes.IsEmpty())
			return null;

		// The two modes bind the cameras and widgets differently, so a mode change rebuilds the view.
		if (s_Instance && s_Instance.m_bMainCameraNV != optimizedView)
		{
			s_Instance.Teardown();
			delete s_Instance;
			s_Instance = null;
		}

		if (!s_Instance)
		{
			s_Instance = ACM_NVGTubeView.Cast(GetGame().SpawnEntity(ACM_NVGTubeView, GetGame().GetWorld()));
			if (!s_Instance)
				return null;

			s_Instance.m_bMainCameraNV = optimizedView;
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
	//! This frame's camera FOV, reported by the local player's camera item from its OnUpdate (see
	//! ACM_CameraFOVReport.c). MEASUREMENT ONLY: compared against the FOV the camera really used, logged
	//! by DiagCameraSync, never applied to the tube cameras.
	protected static float s_fReportedFOV = -1;
	protected static float s_fReportedWorldTime = -1;

	static void ReportCameraFOV(float fov)
	{
		if (fov <= 0 || !GetGame() || !GetGame().GetWorld())
			return;

		s_fReportedFOV = fov;
		s_fReportedWorldTime = GetGame().GetWorld().GetWorldTime();
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
		m_iFramesSinceHook = 0;
		m_iDiagHookCalls++;

		// FOV in the same place as the transform. SyncCamera runs BEFORE the player camera's update
		// each frame, so the FOV it read was always last frame's while the transform here is this
		// frame's: one frame apart. Invisible while the FOV is steady, but in ADS it changes every
		// frame (zoom-in, focus, recoil kick), which made the tube picture jitter against the aim.
		CameraManager cameraManager = GetGame().GetCameraManager();
		if (cameraManager)
		{
			CameraBase mainCamera = cameraManager.CurrentCamera();
			if (mainCamera)
			{
				float fov = mainCamera.GetVerticalFOV();
				m_fHookFOV = fov;

				// Measurement: remember this frame's reported FOV next to what the camera said here.
				if (s_fReportedFOV > 0 && s_fReportedWorldTime == world.GetWorldTime())
				{
					m_fCheckReported = s_fReportedFOV;
					m_fCheckCameraAtHook = fov;
				}
				if (fov > 0 && (fov != m_fAppliedFOV || m_bCropFovDirty))
				{
					m_fAppliedFOV = fov;
					m_bCropFovDirty = false;
					world.SetCameraVerticalFOV(CAMERA_INDEX, CropFOV(fov, m_fNVCropHalfH));
					if (m_bFusionOn)
						world.SetCameraVerticalFOV(FUSION_CAMERA_INDEX, CropFOV(fov, m_fFusionCropHalfH));
				}
			}
		}

		if (!m_bHookSeen)
		{
			m_bHookSeen = true;
			Print("[ACM NVG] Tube camera is now fed directly by the player camera.", LogLevel.DEBUG);
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

		// Default layer, NOT negative. In the retail game the 3D world view is itself a workspace layer
		// at the default order, so a negative z-order (the old -100) put the tubes BEHIND the world and
		// nothing showed - while in Workbench, where the world is not a widget, it looked fine. At the
		// same order, widgets created later draw on top, and the tube view is created after the world.
		m_wRoot.SetZOrder(0);
		UpdateHudLayer();

		BaseWorld world = GetWorld();
		world.SetCameraType(CAMERA_INDEX, CameraType.PERSPECTIVE);
		float buildFar = GetGame().GetViewDistance();
		if (ShowsPeriphery())
			buildFar = Math.Min(buildFar, PERIPHERY_FAR_PLANE);
		world.SetCameraFarPlane(CAMERA_INDEX, buildFar);
		SyncCamera();

		m_fResolutionScale = resolutionScale;

		// Performance mode without the periphery: camera 20 is never bound, so it renders nothing.
		// With the periphery it renders the cheap naked-eye pass shown around the tubes.
		if (!m_bMainCameraNV || PERIPHERY_VIEW)
		{
			float scale = resolutionScale;
			if (m_bMainCameraNV)
			{
				scale = PERIPHERY_RESOLUTION_SCALE;
				world.SetCameraPostProcessEffect(CAMERA_INDEX, PERIPHERY_HDR_SLOT, PostProcessEffectType.HDR, PERIPHERY_HDR);
				world.SetCameraPostProcessEffect(CAMERA_INDEX, PERIPHERY_BLUR_SLOT, PostProcessEffectType.GaussFilter, PERIPHERY_BLUR);
				ApplyPeripheryExposure();
			}

			m_wRenderTarget.SetWorld(world, CAMERA_INDEX);
			m_wRenderTarget.SetRefresh(1, 0);
			m_wRenderTarget.SetResolutionScale(scale, scale);

			// Vanilla PIP always registers the render texture with an entity before it is used; do the
			// same so the engine creates and updates it.
			m_wRenderTexture.SetRenderTarget(this);
			m_bRenderTargetSet = true;
		}

		m_bReady = true;
		ACM_MPDiag.Log("TubeView built: layout ok, rt=" + (m_wRenderTexture != null).ToString() + " target=" + (m_wRenderTarget != null).ToString() + " images frame=" + (m_wImages != null).ToString() + " thermal rt=" + (m_wFusionTexture != null).ToString() + " camera=" + CAMERA_INDEX.ToString() + " scale=" + resolutionScale.ToString());
		Print("[ACM NVG] Tube view created on camera " + CAMERA_INDEX.ToString() + " at resolution scale " + resolutionScale.ToString(4, 2) + ".", LogLevel.DEBUG);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Keeps the view BELOW the HUD but above the 3D world. Created at the workspace it drew over the
	//! HUD (same z-order, created later), which hid it as soon as the surroundings became opaque.
	//! Like vanilla's scope overlay (SCR_2DOpticsDisplay, z-order -1 in the HUD root), it lives in the
	//! HUD root at the very bottom. While the HUD is hidden the HUD root is invisible and would take
	//! the tubes with it, so then the view goes back to the workspace (z-order 0, as before).
	protected static const int HUD_LAYER_Z = -1000;

	protected void UpdateHudLayer()
	{
		// Only needed when the outside is opaque (main-camera modes). The original two-camera view is
		// see-through outside the tubes and stays where it always was: the workspace at z-order 0.
		if (!m_wRoot || !m_bMainCameraNV)
			return;

		WorkspaceWidget workspace = GetGame().GetWorkspace();
		Widget hudRoot;
		SCR_HUDManagerComponent hud = SCR_HUDManagerComponent.GetHUDManager();
		if (hud)
			hudRoot = hud.GetHUDRootWidget();

		if (hudRoot && hudRoot.IsVisible())
		{
			if (m_wRoot.GetParent() != hudRoot)
			{
				hudRoot.AddChild(m_wRoot);
				m_wRoot.SetZOrder(HUD_LAYER_Z);
			}
			return;
		}

		if (workspace && m_wRoot.GetParent() != workspace)
		{
			workspace.AddChild(m_wRoot);
			m_wRoot.SetZOrder(0);
		}
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
			if (!m_bMainCameraNV)
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

		// Camera 20 is ours, but hand it back neutral for the other modes.
		if (ShowsPeriphery() && GetGame() && GetGame().GetWorld())
		{
			GetGame().GetWorld().AdjustCameraEV(CAMERA_INDEX, 0);
			GetGame().GetWorld().SetCameraHDRBrightness(CAMERA_INDEX, -1);
			GetGame().GetWorld().SetCameraPostProcessEffect(CAMERA_INDEX, PERIPHERY_BLUR_SLOT, PostProcessEffectType.None, "");
		}

		m_aOutsideWidgets.Clear();
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
		foreach (ImageWidget oldBand : m_aOutsideWidgets)
		{
			if (oldBand)
				oldBand.RemoveFromHierarchy();
		}
		m_aOutsideWidgets.Clear();

		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if (!workspace || !m_wImages || !m_wRenderTexture)
			return;

		// Edge distortion needs the soft radial mask to blend its rings together. In performance
		// mode there is no separate render to sample, so each tube is a single darkness "hole".
		bool radial = HasRadialMask(workspace);
		int layers = 1;
		if (radial && m_fDistortion > 0 && !m_bMainCameraNV)
			layers = DISTORTION_LAYERS;

		// Performance mode: black bands for everything outside the tubes' clip frames (above and
		// below each tube, gaps between tubes, screen sides). Positioned in UpdateTubeLayout.
		if (m_bMainCameraNV)
		{
			int bandCount = m_aTubes.Count() * 3 + 1;
			for (int b = 0; b < bandCount; b++)
			{
				ImageWidget band = CreateDarkImage(workspace, m_wImages);
				if (band)
					m_aOutsideWidgets.Insert(band);
			}
		}

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

			if (m_bMainCameraNV)
			{
				// Black outside the circle, clear inside (the main camera's night vision shows through).
				ImageWidget hole = CreateDarkImage(workspace, clip);
				if (hole)
				{
					ApplyHoleMask(hole, radial);
					m_aTubeWidgets.Insert(hole);
					m_aLayerTube.Insert(tubeIndex);
					m_aLayerScale.Insert(1.0);
				}

				if (m_fRimStrength > 0)
					CreateRim(workspace, tubeIndex, clip);
				continue;
			}

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

		string rebuilt = "TubeView rebuilt: tubes=" + m_aTubes.Count().ToString() + " clips=" + m_aClipFrames.Count().ToString();
		rebuilt = rebuilt + " images=" + m_aTubeWidgets.Count().ToString() + " rims=" + m_aRimWidgets.Count().ToString();
		rebuilt = rebuilt + " radialMask=" + radial.ToString() + " rimMaskMissing=" + s_bRimMaskLogged.ToString() + " fusion=" + m_bFusionOn.ToString();
		ACM_MPDiag.Log(rebuilt);
		m_iDiagBeats = 0;
		m_fDiagTimer = 0;

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

		// Diagnostics: draw the outline red so it is visible on a dark screen (proves the overlay renders).
		Color rimColor = Color.Black;
		if (ACM_MPDiag.ENABLED)
			rimColor = Color.Red;

		ImageWidget rim = ImageWidget.Cast(workspace.CreateWidget(WidgetType.ImageWidgetTypeID, WidgetFlags.VISIBLE | WidgetFlags.BLEND | WidgetFlags.STRETCH | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS, rimColor, 0, parent));
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
	//! The overlay piece used outside the tubes: the peripheral naked-eye render (PERIPHERY_VIEW) or a
	//! plain black image (no texture draws as solid colour).
	protected ImageWidget CreateDarkImage(WorkspaceWidget workspace, Widget parent)
	{
		bool periphery = ShowsPeriphery() && m_wRenderTexture;
		int flags = WidgetFlags.VISIBLE | WidgetFlags.BLEND | WidgetFlags.STRETCH | WidgetFlags.IGNORE_CURSOR | WidgetFlags.NOFOCUS;
		Color color = Color.Black;
		if (periphery)
		{
			flags |= WidgetFlags.CUSTOMUV;
			color = Color.White;
		}

		ImageWidget image = ImageWidget.Cast(workspace.CreateWidget(WidgetType.ImageWidgetTypeID, flags, color, 0, parent));
		if (!image)
			return null;

		if (periphery)
		{
			image.SetImageTexture(0, m_wRenderTexture);
			image.SetImage(0);
		}
		else
		{
			image.SetOpacity(OUTSIDE_DARKNESS);
		}
		FrameSlot.SetAnchorMin(image, 0, 0);
		FrameSlot.SetAnchorMax(image, 0, 0);
		FrameSlot.SetAlignment(image, 0, 0);
		FrameSlot.SetPos(image, 0, 0);
		FrameSlot.SetSize(image, 0, 0);
		return image;
	}

	//------------------------------------------------------------------------------------------------
	//! The inverse of the tube mask: opaque outside the circle (soft edge), transparent inside.
	protected void ApplyHoleMask(ImageWidget image, bool radial)
	{
		if (radial && image.LoadMaskTexture(MASK_RADIAL))
		{
			// Radial alpha = distance from the centre; INVERTED keeps what is beyond the tube rim.
			image.SetMaskMode(ImageMaskMode.INVERTED);
			float width = Math.Max(m_fSoftness, 0.005);
			image.SetMaskProgress(Math.Clamp(0.98 - width, 0.0, 1.0));
			image.SetMaskTransitionWidth(width);
			return;
		}

		// Hard-edged fallback mask has alpha 1 inside the circle; REGULAR keeps the outside.
		if (image.LoadMaskTexture(MASK_FALLBACK))
		{
			image.SetMaskMode(ImageMaskMode.REGULAR);
			image.SetMaskProgress(0.5);
			image.SetMaskTransitionWidth(0.1);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Performance mode: places the black bands around the tubes. Coordinates in screen heights from
	//! the screen centre, converted to unscaled pixels.
	protected void LayoutOutsideBands(float screenW, float screenH)
	{
		if (m_aOutsideWidgets.IsEmpty())
			return;

		float halfW = screenW * 0.5 / screenH;

		// Clip columns sorted left to right.
		array<int> order = {};
		for (int i = 0; i < m_aTubes.Count(); i++)
		{
			if (!m_aTubes[i])
				continue;

			float lo, hi;
			GetClipBounds(i, lo, hi);
			int insertAt = order.Count();
			for (int k = 0; k < order.Count(); k++)
			{
				float otherLo, otherHi;
				GetClipBounds(order[k], otherLo, otherHi);
				if (lo < otherLo)
				{
					insertAt = k;
					break;
				}
			}
			order.InsertAt(i, insertAt);
		}

		int band = 0;
		float cursor = -halfW;
		foreach (int tubeIndex : order)
		{
			ACM_NVGTube tube = m_aTubes[tubeIndex];
			float clipLo, clipHi;
			GetClipBounds(tubeIndex, clipLo, clipHi);

			// Gap (or the screen's left side) before this column: full height.
			if (clipLo > cursor)
				band = PlaceBand(band, cursor, -0.5, clipLo, 0.5, screenW, screenH);

			// Above and below the tube, across its column.
			band = PlaceBand(band, clipLo, -0.5, clipHi, tube.m_fCenterY - tube.m_fRadius, screenW, screenH);
			band = PlaceBand(band, clipLo, tube.m_fCenterY + tube.m_fRadius, clipHi, 0.5, screenW, screenH);

			cursor = Math.Max(cursor, clipHi);
		}

		// The screen's right side.
		if (cursor < halfW)
			band = PlaceBand(band, cursor, -0.5, halfW, 0.5, screenW, screenH);

		// Unused bands collapse to nothing.
		while (band < m_aOutsideWidgets.Count())
		{
			if (m_aOutsideWidgets[band])
				FrameSlot.SetSize(m_aOutsideWidgets[band], 0, 0);
			band++;
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Screen-space edge (unscaled units) for x / y in screen heights from the centre, rounded so every
	//! overlay piece that shares an edge lands on exactly the same coordinate.
	protected float EdgeX(float x, float screenW, float screenH)
	{
		return Math.Round(screenW * 0.5 + x * screenH);
	}

	protected float EdgeY(float y, float screenH)
	{
		return Math.Round(screenH * 0.5 + y * screenH);
	}

	//------------------------------------------------------------------------------------------------
	protected int PlaceBand(int band, float x0, float y0, float x1, float y1, float screenW, float screenH)
	{
		if (band >= m_aOutsideWidgets.Count())
			return band;

		ImageWidget image = m_aOutsideWidgets[band];
		if (image)
		{
			if (x1 <= x0 || y1 <= y0)
			{
				FrameSlot.SetSize(image, 0, 0);
			}
			else
			{
				// Edges rounded with the same formula as the clip frames, so neighbouring pieces share
				// their edges exactly: no overlap (overlapping see-through darkness showed as lines)
				// and no gap.
				float left = EdgeX(x0, screenW, screenH);
				float top = EdgeY(y0, screenH);
				float width = EdgeX(x1, screenW, screenH) - left;
				float height = EdgeY(y1, screenH) - top;
				FrameSlot.SetPos(image, left, top);
				FrameSlot.SetSize(image, width, height);

				// Periphery view: show the part of the naked-eye render that sits behind this piece.
				if (ShowsPeriphery())
				{
					float uv[4][2];
					uv[0][0] = left / screenW;				uv[0][1] = top / screenH;
					uv[1][0] = (left + width) / screenW;	uv[1][1] = top / screenH;
					uv[2][0] = (left + width) / screenW;	uv[2][1] = (top + height) / screenH;
					uv[3][0] = left / screenW;				uv[3][1] = (top + height) / screenH;
					image.SetUV(uv);
				}
			}
		}

		return band + 1;
	}

	//------------------------------------------------------------------------------------------------
	//! Points every tube image at the render texture again (after it has been allocated / resized).
	protected void RelinkRenderTexture()
	{
		if (!m_wRenderTexture)
			return;

		// Performance mode: the tube images are darkness holes and stay untextured, unless they show
		// the peripheral naked-eye render (then the outside bands need linking as well).
		if (!m_bMainCameraNV || PERIPHERY_VIEW)
		{
			foreach (ImageWidget image : m_aTubeWidgets)
			{
				if (!image)
					continue;

				image.SetImageTexture(0, m_wRenderTexture);
				image.SetImage(0);
			}
		}

		if (ShowsPeriphery())
		{
			foreach (ImageWidget band : m_aOutsideWidgets)
			{
				if (!band)
					continue;

				band.SetImageTexture(0, m_wRenderTexture);
				band.SetImage(0);
			}
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
	// RENDER CROP
	//------------------------------------------------------------------------------------------------
	//! Vertical FOV that shows the centre `halfH` (screen heights) of a view whose full vertical FOV
	//! is `fov` (degrees). Full screen (0.5) returns the FOV unchanged.
	protected float CropFOV(float fov, float halfH)
	{
		if (fov <= 0 || halfH <= 0 || halfH >= 0.5)
			return fov;

		float t = Math.Tan(fov * 0.5 * Math.DEG2RAD) * (halfH / 0.5);
		return 2 * Math.Atan2(t, 1) * Math.RAD2DEG;
	}

	//------------------------------------------------------------------------------------------------
	//! Sizes a render-target texture widget to a screen-centred box (half extents in screen heights).
	protected void SetCropSlot(Widget widget, float halfW, float halfH)
	{
		// Crop off: leave the layout's own full-screen anchoring untouched.
		if (!CROP_RENDERS || !widget || m_fCropScreenH <= 0)
			return;

		float widthPx = halfW * 2 * m_fCropScreenH;
		float heightPx = halfH * 2 * m_fCropScreenH;
		FrameSlot.SetAnchorMin(widget, 0, 0);
		FrameSlot.SetAnchorMax(widget, 0, 0);
		FrameSlot.SetAlignment(widget, 0, 0);
		FrameSlot.SetPos(widget, m_fCropScreenW * 0.5 - widthPx * 0.5, m_fCropScreenH * 0.5 - heightPx * 0.5);
		FrameSlot.SetSize(widget, widthPx, heightPx);
	}

	//------------------------------------------------------------------------------------------------
	//! Night vision crop: the box around all tubes, widened for the edge-distortion rings (they sample
	//! up to m_fDistortion beyond the tube) plus a little slack for filtering.
	protected void ComputeTubeCrop(float screenW, float screenH)
	{
		m_fCropScreenW = screenW;
		m_fCropScreenH = screenH;
		float maxHalfW = screenW * 0.5 / screenH;

		m_fNVCropHalfW = maxHalfW;
		m_fNVCropHalfH = 0.5;

		// Main-camera modes use camera 20 for full-screen pieces (or not at all): no crop.
		if (CROP_RENDERS && !m_bMainCameraNV)
		{
			float halfW = 0;
			float halfH = 0;
			float margin = 1.0 + m_fDistortion;
			foreach (ACM_NVGTube tube : m_aTubes)
			{
				if (!tube)
					continue;

				halfW = Math.Max(halfW, Math.AbsFloat(tube.m_fCenterX) + tube.m_fRadius * margin);
				halfH = Math.Max(halfH, Math.AbsFloat(tube.m_fCenterY) + tube.m_fRadius * margin);
			}

			if (halfW > 0 && halfH > 0)
			{
				m_fNVCropHalfW = Math.Min(halfW + 0.01, maxHalfW);
				m_fNVCropHalfH = Math.Min(halfH + 0.01, 0.5);
			}
		}

		SetCropSlot(m_wRenderTexture, m_fNVCropHalfW, m_fNVCropHalfH);
		m_bCropFovDirty = true;
	}

	//------------------------------------------------------------------------------------------------
	//! Thermal crop: the box around the fused circle in the centre tube.
	protected void ComputeFusionCrop()
	{
		if (m_fCropScreenH <= 0)
			return;

		float maxHalfW = m_fCropScreenW * 0.5 / m_fCropScreenH;
		m_fFusionCropHalfW = maxHalfW;
		m_fFusionCropHalfH = 0.5;

		int center = GetCenterTubeIndex();
		if (CROP_RENDERS && center >= 0 && m_aTubes[center])
		{
			ACM_NVGTube tube = m_aTubes[center];
			float radius = tube.m_fRadius * m_fFusionRadius;
			m_fFusionCropHalfW = Math.Min(Math.AbsFloat(tube.m_fCenterX) + radius + 0.01, maxHalfW);
			m_fFusionCropHalfH = Math.Min(Math.AbsFloat(tube.m_fCenterY) + radius + 0.01, 0.5);
		}

		SetCropSlot(m_wFusionTexture, m_fFusionCropHalfW, m_fFusionCropHalfH);
		m_bCropFovDirty = true;
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

		// Crop boxes first: the image UVs below map into them.
		ComputeTubeCrop(workspace.DPIUnscale(pixelWidth), workspace.DPIUnscale(pixelHeight));

		// Screen size changed: the fused circle follows the centre tube.
		PositionFusionImage();

		// A resize re-creates the render texture, so re-link the images over the next frames.
		m_iFramesSinceBuild = 0;

		float screenW = workspace.DPIUnscale(pixelWidth);
		float screenH = workspace.DPIUnscale(pixelHeight);

		if (m_bMainCameraNV)
			LayoutOutsideBands(screenW, screenH);

		// Clip frames: each tube's box, cut at the straight seams with its neighbours.
		for (int c = 0; c < m_aClipFrames.Count(); c++)
		{
			Widget clip = m_aClipFrames[c];
			if (!clip || !m_aTubes.IsIndexValid(c) || !m_aTubes[c])
				continue;

			ACM_NVGTube clipTube = m_aTubes[c];
			float clipLo, clipHi;
			GetClipBounds(c, clipLo, clipHi);

			if (m_bMainCameraNV)
			{
				// Rounded shared edges (see EdgeX): seams between clip frames and the outside bands line
				// up exactly, so the see-through darkness never doubles up into visible lines.
				float clipLeft = EdgeX(clipLo, screenW, screenH);
				float clipTop = EdgeY(clipTube.m_fCenterY - clipTube.m_fRadius, screenH);
				FrameSlot.SetPos(clip, clipLeft, clipTop);
				FrameSlot.SetSize(clip, EdgeX(clipHi, screenW, screenH) - clipLeft, EdgeY(clipTube.m_fCenterY + clipTube.m_fRadius, screenH) - clipTop);
			}
			else
			{
				FrameSlot.SetPos(clip, screenW * 0.5 + clipLo * screenH, screenH * 0.5 + (clipTube.m_fCenterY - clipTube.m_fRadius) * screenH);
				FrameSlot.SetSize(clip, (clipHi - clipLo) * screenH, clipTube.m_fRadius * 2 * screenH);
			}
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
			if (m_bMainCameraNV)
			{
				// Darkness hole: the clip frame is snapped to whole pixels, the tube size is not, so an
				// exact-size hole fell up to a pixel short of the frame's right/bottom edge and the night
				// vision showed through as a thin line. One pixel of overhang on every side; the clip
				// frame cuts it off, so it always reaches the frame edges.
				FrameSlot.SetPos(image, (tube.m_fCenterX - tube.m_fRadius - tubeLo) * screenH - 1, -1);
				FrameSlot.SetSize(image, size + 2, size + 2);
			}
			else
			{
				FrameSlot.SetPos(image, (tube.m_fCenterX - tube.m_fRadius - tubeLo) * screenH, 0);
				FrameSlot.SetSize(image, size, size);
			}

			// Sample the region behind the tube; distortion rings sample a slightly larger area
			// around the same centre, which squeezes the picture towards the rim. The render covers
			// only the crop box, so UVs are relative to that box.
			float nvWidth = m_fNVCropHalfW * 2 * screenH;
			float nvHeight = m_fNVCropHalfH * 2 * screenH;
			float nvLeft = screenW * 0.5 - nvWidth * 0.5;
			float nvTop = screenH * 0.5 - nvHeight * 0.5;

			float scale = m_aLayerScale[i];
			float centerU = (left + size * 0.5 - nvLeft) / nvWidth;
			float centerV = (top + size * 0.5 - nvTop) / nvHeight;
			float halfU = size * 0.5 * scale / nvWidth;
			float halfV = size * 0.5 * scale / nvHeight;

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
	//! FOV read at the hook, and post-frames since the hook last ran (hook is "live" for ~3 frames).
	protected float m_fHookFOV = -1;
	protected int m_iFramesSinceHook = 1000;

	//! Camera sync diagnostics (TEMPORARY): logged every 5 s for the first minute of a tube view.
	protected int m_iDiagHookCalls;
	protected int m_iDiagFallbackWrites;
	protected int m_iDiagFovLate;
	protected int m_iDiagFrames;

	//! Reported-FOV measurement (ACM_CameraFOVReport.c). Not applied, only logged.
	protected float m_fCheckReported = -1;
	protected float m_fCheckCameraAtHook = -1;
	protected int m_iDiagReports;
	protected int m_iDiagReportedMatch;
	protected float m_fDiagReportedMaxMiss;
	protected int m_iDiagHookStale;
	protected string m_sDiagSample;
	protected int m_iDiagSyncLogs;
	protected float m_fDiagSyncTimer;

	//------------------------------------------------------------------------------------------------
	protected void DiagCameraSync(float timeSlice)
	{
		if (m_iDiagSyncLogs >= 24)
			return;

		m_iDiagFrames++;
		m_fDiagSyncTimer += timeSlice;
		if (m_fDiagSyncTimer < 5)
			return;

		m_iDiagSyncLogs++;
		string line = "[ACM NVG] Tube camera sync (5 s): frames=" + m_iDiagFrames.ToString();
		line = line + " hookCalls=" + m_iDiagHookCalls.ToString();
		line = line + " fallbackWrites=" + m_iDiagFallbackWrites.ToString();
		line = line + " fovChangedAfterHook=" + m_iDiagFovLate.ToString();
		Print(line, LogLevel.WARNING);

		// Measurement result: reportedMatchesFinal should equal reports; hookStale > 0 = frames where
		// the value in use today was a frame late (those are the jitter frames).
		string fovLine = "[ACM NVG] FOV check (5 s): reports=" + m_iDiagReports.ToString();
		fovLine = fovLine + " reportedMatchesFinal=" + m_iDiagReportedMatch.ToString();
		fovLine = fovLine + " reportedMaxMiss=" + m_fDiagReportedMaxMiss.ToString();
		fovLine = fovLine + " hookStale=" + m_iDiagHookStale.ToString();
		if (!m_sDiagSample.IsEmpty())
			fovLine = fovLine + " sample[" + m_sDiagSample + "]";
		Print(fovLine, LogLevel.WARNING);

		m_fDiagSyncTimer = 0;
		m_iDiagFrames = 0;
		m_iDiagHookCalls = 0;
		m_iDiagFallbackWrites = 0;
		m_iDiagFovLate = 0;
		m_iDiagReports = 0;
		m_iDiagReportedMatch = 0;
		m_fDiagReportedMaxMiss = 0;
		m_iDiagHookStale = 0;
		m_sDiagSample = string.Empty;
	}

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
		{
			world.SetCameraEx(CAMERA_INDEX, mat);
			m_iDiagFallbackWrites++;
		}

		// The hook sets the FOV together with the transform. Only drive it from here when the hook is
		// not running (vehicle cameras, spectating): this read is last frame's FOV.
		m_iFramesSinceHook++;
		bool hookLive = m_bHookSeen && m_iFramesSinceHook <= 3;

		// Measurement: this runs before the player camera's update, so `fov` is the FOV the camera
		// really ended last frame with. Did the reported value (or the hook-time value) match it?
		if (m_fCheckReported > 0 && fov > 0)
		{
			m_iDiagReports++;
			float missReported = Math.AbsFloat(fov - m_fCheckReported);
			float missHook = Math.AbsFloat(fov - m_fCheckCameraAtHook);
			if (missReported < 0.01)
				m_iDiagReportedMatch++;
			else
				m_fDiagReportedMaxMiss = Math.Max(m_fDiagReportedMaxMiss, missReported);

			if (missHook >= 0.01)
			{
				m_iDiagHookStale++;
				m_sDiagSample = "reported=" + m_fCheckReported.ToString() + " atHook=" + m_fCheckCameraAtHook.ToString() + " final=" + fov.ToString();
			}

			m_fCheckReported = -1;
		}
		if (hookLive && m_fHookFOV > 0 && fov != m_fHookFOV)
			m_iDiagFovLate++;

		// m_fAppliedFOV is the main view's FOV; each camera gets the narrower FOV of its crop box.
		if (!hookLive && (fov != m_fAppliedFOV || m_bCropFovDirty))
		{
			m_fAppliedFOV = fov;
			m_bCropFovDirty = false;
			world.SetCameraVerticalFOV(CAMERA_INDEX, CropFOV(fov, m_fNVCropHalfH));
		}

		if (nearPlane != m_fAppliedNear && nearPlane > 0)
		{
			m_fAppliedNear = nearPlane;
			world.SetCameraNearPlane(CAMERA_INDEX, nearPlane);
		}

		// Follow the player's view distance setting if it changes while the view is up.
		// m_fAppliedFar is the full view distance (the ECOTI camera uses it); the peripheral naked-eye
		// pass is capped shorter, which cuts how much of the world it has to draw.
		float farPlane = GetGame().GetViewDistance();
		if (farPlane != m_fAppliedFar && farPlane > 0)
		{
			m_fAppliedFar = farPlane;
			float tubeFar = farPlane;
			if (ShowsPeriphery())
				tubeFar = Math.Min(farPlane, PERIPHERY_FAR_PLANE);
			world.SetCameraFarPlane(CAMERA_INDEX, tubeFar);
		}

		// The fused thermal camera always matches the night vision camera exactly.
		if (m_bFusionOn)
		{
			if (!m_bHookSeen || m_fHookWorldTime != world.GetWorldTime())
				world.SetCameraEx(FUSION_CAMERA_INDEX, mat);

			if (!hookLive)
				world.SetCameraVerticalFOV(FUSION_CAMERA_INDEX, CropFOV(m_fAppliedFOV, m_fFusionCropHalfH));
			if (m_fAppliedNear > 0)
				world.SetCameraNearPlane(FUSION_CAMERA_INDEX, m_fAppliedNear);
			if (m_fAppliedFar > 0)
				world.SetCameraFarPlane(FUSION_CAMERA_INDEX, m_fAppliedFar);

			// Keep the thermal exposure locked (cheap; nothing else may hand it back to automatic).
			world.SetCameraHDRBrightness(FUSION_CAMERA_INDEX, FUSION_EXPOSURE);
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
				world.SetCameraVerticalFOV(FUSION_CAMERA_INDEX, CropFOV(m_fAppliedFOV, m_fFusionCropHalfH));
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

		world.SetCameraHDRBrightness(FUSION_CAMERA_INDEX, FUSION_EXPOSURE);

		if (!m_wFusionImage)
			CreateFusionImage();
		else
		{
			m_wFusionImage.SetOpacity(m_fFusionOpacity);
			PositionFusionImage();
		}

		Print("[ACM NVG] Fused thermal on (centre tube, " + (m_fFusionRadius * 100).ToString() + "% circle, " + (m_fFusionOpacity * 100).ToString() + "% opacity).", LogLevel.DEBUG);
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

		if (world)
			world.SetCameraHDRBrightness(FUSION_CAMERA_INDEX, -1);

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

		// The thermal render covers only its crop box; UVs are relative to that box.
		ComputeFusionCrop();
		float fusionWidth = m_fFusionCropHalfW * 2 * screenH;
		float fusionHeight = m_fFusionCropHalfH * 2 * screenH;
		float fusionLeft = screenW * 0.5 - fusionWidth * 0.5;
		float fusionTop = screenH * 0.5 - fusionHeight * 0.5;
		float u0 = (left - fusionLeft) / fusionWidth;
		float u1 = (left + size - fusionLeft) / fusionWidth;
		float v0 = (top - fusionTop) / fusionHeight;
		float v1 = (top + size - fusionTop) / fusionHeight;

		float uv[4][2];
		uv[0][0] = u0;	uv[0][1] = v0;
		uv[1][0] = u1;	uv[1][1] = v0;
		uv[2][0] = u1;	uv[2][1] = v1;
		uv[3][0] = u0;	uv[3][1] = v1;
		m_wFusionImage.SetUV(uv);
	}

	//------------------------------------------------------------------------------------------------
	override protected void EOnPostFrame(IEntity owner, float timeSlice)
	{
		if (!m_bReady)
			return;

		if (CheckOwnerStillWorn(timeSlice))
			return;

		SyncCamera();
		UpdateHudLayer();
		UpdateTubeLayout();

		// The engine only refreshes the render texture when an image links to it (it stayed live for
		// exactly the frames it was re-linked and froze afterwards), so link it every frame.
		RelinkRenderTexture();

		DiagHeartbeat(timeSlice);
		DiagCameraSync(timeSlice);
	}

	//------------------------------------------------------------------------------------------------
	// OWNER WATCHDOG
	//
	// Taking off the helmet / headstrap with the goggles still on and switched on never reached the
	// device (no ModeSwitch for a device still in its mount, and its update stops once the mount is in
	// storage), so the tubes stayed on screen. This view ticks locally every frame, so it checks the
	// owner a few times a second and has it switch off after a few failed checks in a row (one or two
	// can be a brief inventory transfer). The switch-off runs on the call queue, not inside this
	// event, because it deletes this entity.
	//------------------------------------------------------------------------------------------------
	//! Every frame, first miss: the picture goes the moment the goggles come off (user request).
	protected static const float OWNER_CHECK_INTERVAL = 0;
	protected static const int OWNER_MISSES_TO_RELEASE = 1;
	protected float m_fOwnerCheckTimer;
	protected int m_iOwnerMisses;

	//! Returns true when the view is being shut down (skip the rest of the frame).
	protected bool CheckOwnerStillWorn(float timeSlice)
	{
		if (OWNER_CHECK_INTERVAL > 0)
		{
			m_fOwnerCheckTimer += timeSlice;
			if (m_fOwnerCheckTimer < OWNER_CHECK_INTERVAL)
				return false;
			m_fOwnerCheckTimer = 0;
		}

		// Owner gone entirely (device deleted without cleaning up): drop the view itself.
		if (!m_Owner)
		{
			GetGame().GetCallqueue().CallLater(ReleaseOrphan, 0, false);
			return true;
		}

		ACM_NightVisionComponent nvg = ACM_NightVisionComponent.Cast(m_Owner);
		if (!nvg)
			return false;

		if (nvg.ACM_IsStillWorn())
		{
			m_iOwnerMisses = 0;
			return false;
		}

		m_iOwnerMisses++;
		if (m_iOwnerMisses < OWNER_MISSES_TO_RELEASE)
			return false;

		m_iOwnerMisses = 0;

		// Hide the tubes in this very frame; the full shutdown follows on the next call-queue tick.
		if (m_wRoot)
			m_wRoot.SetVisible(false);

		GetGame().GetCallqueue().CallLater(nvg.ACM_ForceOff, 0, false);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	static void ReleaseOrphan()
	{
		if (!s_Instance || s_Instance.m_Owner)
			return;

		s_Instance.Teardown();
		delete s_Instance;
		s_Instance = null;
	}

	//------------------------------------------------------------------------------------------------
	// TEMPORARY diagnostics: a few status lines after each rebuild (see ACM_MPDiag).
	protected int m_iDiagBeats;
	protected float m_fDiagTimer;

	protected void DiagHeartbeat(float timeSlice)
	{
		if (!ACM_MPDiag.ENABLED || m_iDiagBeats >= 4)
			return;

		m_fDiagTimer += timeSlice;
		if (m_fDiagTimer < 1.5)
			return;

		m_fDiagTimer = 0;
		m_iDiagBeats++;

		WorkspaceWidget workspace = GetGame().GetWorkspace();
		string ws = "none";
		if (workspace)
			ws = workspace.GetWidth().ToString() + "x" + workspace.GetHeight().ToString();

		string root = "null";
		if (m_wRoot)
			root = "visible=" + m_wRoot.IsVisible().ToString();

		string img = "none";
		if (!m_aTubeWidgets.IsEmpty() && m_aTubeWidgets[0])
		{
			float x, y, w, h;
			m_aTubeWidgets[0].GetScreenPos(x, y);
			m_aTubeWidgets[0].GetScreenSize(w, h);
			img = "pos=" + x.ToString() + "," + y.ToString() + " size=" + w.ToString() + "x" + h.ToString() + " visible=" + m_aTubeWidgets[0].IsVisible().ToString();
		}

		string rt = "null";
		if (m_wRenderTexture)
		{
			float rw, rh;
			m_wRenderTexture.GetScreenSize(rw, rh);
			rt = rw.ToString() + "x" + rh.ToString() + " visible=" + m_wRenderTexture.IsVisible().ToString();
		}

		vector cam[4];
		GetWorld().GetCamera(CAMERA_INDEX, cam);

		string beat = m_iDiagBeats.ToString();
		string line = "TubeView beat " + beat + ": workspace=" + ws;
		line = line + " root[" + root + "]";
		line = line + " image0[" + img + "]";
		line = line + " rtWidget[" + rt + "]";
		line = line + " targetSet=" + m_bRenderTargetSet.ToString();
		line = line + " hookSeen=" + m_bHookSeen.ToString();
		line = line + " cam20pos=" + cam[3].ToString();
		ACM_MPDiag.Log(line);
	}
}
