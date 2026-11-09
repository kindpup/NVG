[ComponentEditorProps(category: "GameScripted/Gadgets", description: "Independent screen-space dual-tube NVG overlay.")]
class ACM_NVGScreenTubeOverlayComponentClass : ScriptComponentClass
{
}

// This is deliberately independent of SCR_PIPSightsComponent. Head-worn NVGs
// are not weapon sights and must not wait for an ADS/sight-selection callback.
class ACM_NVGScreenTubeOverlayComponent : ScriptComponent
{
	[Attribute("{E3F1A9B2C4D74A62}UI/Layouts/ACM_NVG_DualTubeOverlay.layout", UIWidgets.ResourceNamePicker, "Full-screen HUD layout.", "layout")]
	protected ResourceName m_sLayoutResource;
	[Attribute("NVGFeedTexture", UIWidgets.EditBox, "RT texture widget name.")]
	protected string m_sRTTextureWidgetName;
	[Attribute("NVGFeedTarget", UIWidgets.EditBox, "Render target widget name.")]
	protected string m_sRTargetWidgetName;
	[Attribute("20", UIWidgets.EditBox, "Dedicated secondary camera index (0-31).")]
	protected int m_iCameraIndex;
	[Attribute("65", UIWidgets.EditBox, "Secondary camera vertical FOV.")]
	protected float m_fCameraFOV;
	[Attribute("0.2", UIWidgets.EditBox, "Secondary camera near plane.")]
	protected float m_fNearPlane;
	[Attribute("5000", UIWidgets.EditBox, "Secondary camera far plane.")]
	protected float m_fFarPlane;
	[Attribute("1", UIWidgets.EditBox, "Render target resolution scale.")]
	protected float m_fResolutionScale;
	[Attribute("0 0 0", UIWidgets.EditBox, "Local camera offset from the head-mounted item.")]
	protected vector m_vCameraPoint;
	[Attribute("{D9407BBA0E4848A7}Textures/ACM_NVG_TubeMask.edds", UIWidgets.ResourceNamePicker, "Soft circular alpha mask.", "edds")]
	protected ResourceName m_sTubeMaskTexture;

	protected IEntity m_pOwner;
	protected BaseWorld m_World;
	protected ScriptCamera m_TubeCamera;
	protected Widget m_wRoot;
	protected RTTextureWidget m_wFeedTexture;
	protected RenderTargetWidget m_wRenderTarget;
	protected ImageWidget m_wLeftTube;
	protected ImageWidget m_wRightTube;
	protected bool m_bFeedActive;
	protected bool m_bReady;
	protected bool m_bFailureLogged;

	void ACM_NVGScreenTubeOverlayComponent(IEntityComponentSource src, IEntity ent, IEntity parent)
	{
		SetEventMask(ent, EntityEvent.INIT | EntityEvent.FRAME);
	}

	override void EOnInit(IEntity owner)
	{
		m_pOwner = owner;
		m_World = owner.GetWorld();
		CreateOverlay();
	}

	void SetFeedActive(bool active)
	{
		m_bFeedActive = active;
		if (m_wRoot)
			m_wRoot.SetVisible(active && m_bReady);
	}

	bool IsFeedReady()
	{
		return m_bReady && m_World && m_TubeCamera && m_wRoot && m_wLeftTube && m_wRightTube && m_wRenderTarget;
	}

	int GetFeedCameraIndex()
	{
		return m_iCameraIndex;
	}

	void UpdateFeedCameraFromView(int viewCameraId)
	{
		if (!m_bReady || !m_pOwner || !m_World || !m_TubeCamera || viewCameraId < 0)
			return;

		vector viewTransform[4];
		m_World.GetCamera(viewCameraId, viewTransform);
		m_TubeCamera.SetWorldTransform(viewTransform);
	}

	protected void CreateOverlay()
	{
		if (!m_pOwner || !m_World || !GetGame() || !GetGame().GetWorkspace())
			return;

		WorkspaceWidget workspace = GetGame().GetWorkspace();
		m_wRoot = workspace.CreateWidgets(m_sLayoutResource);
		if (!m_wRoot)
		{
			LogFailure("HUD layout failed to load.");
			return;
		}

		m_wFeedTexture = RTTextureWidget.Cast(m_wRoot.FindAnyWidget(m_sRTTextureWidgetName));
		m_wRenderTarget = RenderTargetWidget.Cast(m_wRoot.FindAnyWidget(m_sRTargetWidgetName));
		m_wLeftTube = ImageWidget.Cast(m_wRoot.FindAnyWidget("NVGLeftTube"));
		m_wRightTube = ImageWidget.Cast(m_wRoot.FindAnyWidget("NVGRightTube"));
		if (!m_wFeedTexture || !m_wRenderTarget || !m_wLeftTube || !m_wRightTube)
		{
			LogFailure("HUD layout is missing a feed or tube widget.");
			return;
		}

		// The stock PIP flow uses a real ScriptCamera entity. Parenting it to the
		// worn item makes the feed independent of weapon ADS/sight selection.
		m_TubeCamera = ScriptCamera.Cast(GetGame().SpawnEntity(ScriptCamera, m_World));
		if (!m_TubeCamera)
		{
			LogFailure("NVG feed camera could not be created.");
			return;
		}

		vector cameraLocalTransform[4];
		Math3D.MatrixIdentity3(cameraLocalTransform);
		cameraLocalTransform[3] = m_vCameraPoint;
		m_TubeCamera.SetTransform(cameraLocalTransform);
		m_TubeCamera.Index = m_iCameraIndex;
		m_TubeCamera.FreeFly = false;
		m_pOwner.AddChild(m_TubeCamera, -1, EAddChildFlags.AUTO_TRANSFORM);
		m_World.SetCameraVerticalFOV(m_iCameraIndex, m_fCameraFOV);
		m_World.SetCameraNearPlane(m_iCameraIndex, m_fNearPlane);
		m_World.SetCameraFarPlane(m_iCameraIndex, m_fFarPlane);

		m_wRenderTarget.SetResolutionScale(m_fResolutionScale, m_fResolutionScale);
		m_wRenderTarget.SetRefresh(1, 0);
		m_wRenderTarget.SetWorld(m_World, m_iCameraIndex);
		// The RT widget is consumed directly by the two HUD images below.
		// Do not register it on the wearable entity: SetRenderTarget is the
		// optic/material path and depends on a selected owner object.
		m_wLeftTube.SetImageTexture(0, m_wFeedTexture);
		m_wRightTube.SetImageTexture(0, m_wFeedTexture);
		if (!m_wLeftTube.LoadMaskTexture(m_sTubeMaskTexture) || !m_wRightTube.LoadMaskTexture(m_sTubeMaskTexture))
		{
			LogFailure("Circular tube mask failed to load.");
			return;
		}

		// The hidden source widgets keep the off-screen render alive; the masked
		// images are the only visible result on the full-screen HUD.
		m_wFeedTexture.SetOpacity(0.01);
		m_wRenderTarget.SetOpacity(0.01);
		m_wRoot.SetVisible(false);
		m_bReady = true;
		Print("[ACM NVG] Independent screen-space dual-tube HUD initialized.");
	}

	protected void LogFailure(string reason)
	{
		if (m_bFailureLogged)
			return;
		m_bFailureLogged = true;
		Print("[ACM NVG] Dual-tube HUD unavailable: " + reason, LogLevel.ERROR);
	}

	override void EOnFrame(IEntity owner, float timeSlice)
	{
		// A wearable can initialize before the local HUD workspace exists.
		// Retry only while no layout has been created; partial setup failures
		// remain logged and are not rebuilt every frame.
		if (!m_bReady && !m_wRoot && owner && GetGame() && GetGame().GetWorkspace())
			CreateOverlay();

		if (!m_bReady || !m_bFeedActive || !m_World)
			return;
		UpdateFeedCameraFromView(m_World.GetCurrentCameraId());
		if (m_wRoot && !m_wRoot.IsVisible())
			m_wRoot.SetVisible(true);
	}

	override void OnDelete(IEntity owner)
	{
		m_bFeedActive = false;
		m_bReady = false;
		// The HUD image owns the feed link; no render target was registered on
		// the wearable entity, so teardown only needs to remove the HUD widgets.
		if (m_TubeCamera && !m_TubeCamera.IsDeleted())
		{
			IEntity cameraParent = m_TubeCamera.GetParent();
			if (cameraParent && !cameraParent.IsDeleted())
				cameraParent.RemoveChild(m_TubeCamera);
			delete m_TubeCamera;
			m_TubeCamera = null;
		}
		if (m_wRoot)
			m_wRoot.RemoveFromHierarchy();
		m_wLeftTube = null;
		m_wRightTube = null;
		m_wFeedTexture = null;
		m_wRenderTarget = null;
		m_wRoot = null;
		m_pOwner = null;
		m_World = null;
		super.OnDelete(owner);
	}
}
