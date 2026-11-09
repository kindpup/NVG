// ACM IR Laser Entity
// Specialized entity for infrared laser beam
// Uses mesh-based beam with raycasting for proper length
// Only visible when NVGs are active (unless IR filter is removed)
// 
// APPROACH: We spawn a child mesh entity and control its visibility
// This prevents external systems from interfering with our visibility control

[EntityEditorProps(category: "ACM/Lights", description: "IR Laser Beam with mesh and raycasting")]
class ACM_IRLaserClass : GenericEntityClass {}

class ACM_IRLaser : GenericEntity
{
	// Mesh configuration
	[Attribute("", UIWidgets.ResourcePickerThumbnail, desc: "Beam mesh prefab or xob", params: "et xob")]
	protected ResourceName m_sBeamMesh;
	
	// Range configuration
	[Attribute("500", UIWidgets.Slider, desc: "Maximum laser range in meters", params: "10 1000 10")]
	protected float m_fMaxRange;
	
	[Attribute("1.0", UIWidgets.EditBox, desc: "Default mesh length in meters (for scaling calculation)")]
	protected float m_fMeshDefaultLength;
	
	// Color configuration
	[Attribute("0.9 1.0 0.9", UIWidgets.ColorPicker, desc: "IR beam color (visible through NVG)")]
	protected vector m_vIRColor;
	
	[Attribute("1 0 0", UIWidgets.ColorPicker, desc: "Visible beam color (when filter removed)")]
	protected vector m_vVisibleColor;
	
	[Attribute("200.0", UIWidgets.Slider, desc: "Emissive intensity", params: "1 500 10")]
	protected float m_fIntensity;
	
	[Attribute("0.5", UIWidgets.Slider, desc: "Beam opacity/translucency (0 = transparent, 1 = solid)", params: "0.01 1 0.01")]
	protected float m_fBeamOpacity;
	
	[Attribute("0.005", UIWidgets.Slider, desc: "Beam thickness/width in meters", params: "0.001 0.1 0.001")]
	protected float m_fBeamThickness;
	
	// Runtime state - these are the ONLY things that control visibility
	protected bool m_bIREnabled = false;      // User toggled IR on/off
	protected bool m_bNVGActive = false;       // NVG is active
	protected bool m_bIRFilterRemoved = false; // IR filter removed (visible without NVG)
	protected bool m_bInitialized = false;
	// FPS patch: throttle expensive beam raycasts.
	protected float m_fBeamTraceTimer;
	protected static const float BEAM_TRACE_INTERVAL = 0.05;
	
	// The actual beam mesh entity (spawned as child)
	protected IEntity m_BeamMeshEntity;
	
	// Material component for color control
	protected ParametricMaterialInstanceComponent m_BeamMaterial;
	
	// Raycast results
	protected vector m_vHitPoint;
	protected float m_fCurrentLength;
	protected bool m_bHitSomething;
	
	//------------------------------------------------------------------------------------------------
	void ACM_IRLaser(IEntitySource src, IEntity parent)
	{
		// Removed verbose logging - only log warnings/errors
		
		// CRITICAL: Force hidden on construction
		m_bIREnabled = false;
		m_bNVGActive = false;
		m_bIRFilterRemoved = false;
		
		// ACM DS SAFETY: no local IR beam entity/frame logic on dedicated server.
		if (Replication.IsRunning() && Replication.IsServer() && !GetGame().GetPlayerController())
			return;

		// FPS patch: initialize only. FRAME is enabled only while the beam is actually visible.
		SetEventMask(EntityEvent.INIT);
	}
	
	//------------------------------------------------------------------------------------------------
	override void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);

		if (Replication.IsRunning() && Replication.IsServer() && !GetGame().GetPlayerController())
			return;
		
		// Removed verbose logging - only log warnings/errors
		
		// Spawn the beam mesh as a child entity
		if (!m_sBeamMesh.IsEmpty())
		{
			SpawnBeamMesh();
		}
		
		// Defer full initialization
		GetGame().GetCallqueue().CallLater(DeferredInit, 100, false);
	}
	
	//------------------------------------------------------------------------------------------------
	protected void SpawnBeamMesh()
	{
		Resource res = Resource.Load(m_sBeamMesh);
		if (!res || !res.IsValid())
		{
			return;
		}
		
		// Check if it's a prefab (.et) or mesh (.xob)
		string path = m_sBeamMesh;
		bool isPrefab = path.Contains(".et");
		
		if (isPrefab)
		{
			// Spawn as prefab
			m_BeamMeshEntity = GetGame().SpawnEntityPrefabLocal(res);
		}
		else
		{
			// It's an xob - we can't spawn it directly
			return;
		}
		
		if (!m_BeamMeshEntity)
		{
			return;
		}
		
		// Add as child with auto transform
		AddChild(m_BeamMeshEntity, -1, EAddChildFlags.AUTO_TRANSFORM);
		
		// Reset local transform
		vector mat[4];
		Math3D.MatrixIdentity4(mat);
		m_BeamMeshEntity.SetLocalTransform(mat);
		
		// CRITICAL: Start hidden
		m_BeamMeshEntity.ClearFlags(EntityFlags.VISIBLE | EntityFlags.ACTIVE);
		
		// Defer material search to allow component initialization
		GetGame().GetCallqueue().CallLater(FindAndSetupMaterial, 50, false);
		
		// Removed verbose logging - only log warnings/errors
	}
	
	//------------------------------------------------------------------------------------------------
	protected void FindAndSetupMaterial()
	{
		if (!m_BeamMeshEntity)
			return;
		
		// Get material component for color control
		m_BeamMaterial = ParametricMaterialInstanceComponent.Cast(m_BeamMeshEntity.FindComponent(ParametricMaterialInstanceComponent));
		
		// If not found directly, search children
		if (!m_BeamMaterial)
		{
			IEntity child = m_BeamMeshEntity.GetChildren();
			while (child)
			{
				m_BeamMaterial = ParametricMaterialInstanceComponent.Cast(child.FindComponent(ParametricMaterialInstanceComponent));
				if (m_BeamMaterial)
					break;
				child = child.GetSibling();
			}
		}
		
		if (m_BeamMaterial)
		{
			// Removed verbose logging - only log warnings/errors
			ApplyBeamColor();
		}
		else
		{
		}
	}
	
	//------------------------------------------------------------------------------------------------
	protected void DeferredInit()
	{
		if (m_bInitialized)
			return;
		
		m_bInitialized = true;
		
		// Removed verbose logging - only log warnings/errors
		
		// Register with IR system
		RegisterWithIRSystem();
		
		// CRITICAL: Force visibility update based on current state
		ForceUpdateVisibility();
	}
	
	//------------------------------------------------------------------------------------------------
	protected void RegisterWithIRSystem()
	{
		ACM_IRSystem sys = ACM_IRSystem.GetInstance();
		if (sys)
		{
			sys.TrackLaser(this);
			m_bNVGActive = sys.IsIRVisible();
			// Removed verbose logging - only log warnings/errors
			
			// Force visibility update with current state
			ForceUpdateVisibility();
		}
		else
		{
			// Retry registration after a delay
			GetGame().GetCallqueue().CallLater(RegisterWithIRSystem, 200, false);
		}
	}
	
	//------------------------------------------------------------------------------------------------
	protected void UnregisterFromIRSystem()
	{
		ACM_IRSystem sys = ACM_IRSystem.GetInstance();
		if (sys)
			sys.UntrackLaser(this);
	}
	
	//------------------------------------------------------------------------------------------------
	override void EOnFrame(IEntity owner, float timeSlice)
	{
		bool shouldBeVisible = ShouldBeVisible();
		
		if (m_BeamMeshEntity)
		{
			if (m_BeamMeshEntity.GetParent() != this)
				AddChild(m_BeamMeshEntity, -1, EAddChildFlags.AUTO_TRANSFORM);
			
			if (!shouldBeVisible)
			{
				m_BeamMeshEntity.ClearFlags(EntityFlags.VISIBLE | EntityFlags.ACTIVE);
				SetEventMask(EntityEvent.INIT);
				return;
			}
			
			m_BeamMeshEntity.SetFlags(EntityFlags.VISIBLE | EntityFlags.ACTIVE);
		}
		else
		{
			if (!shouldBeVisible)
			{
				ClearFlags(EntityFlags.VISIBLE | EntityFlags.ACTIVE);
				SetScale(0.0001);
				SetEventMask(EntityEvent.INIT);
				return;
			}
			
			SetFlags(EntityFlags.VISIBLE | EntityFlags.ACTIVE);
		}
		
		m_fBeamTraceTimer += timeSlice;
		if (m_fBeamTraceTimer < BEAM_TRACE_INTERVAL)
			return;
		
		m_fBeamTraceTimer = 0;
		UpdateBeamLength();
	}
	
	//------------------------------------------------------------------------------------------------
	protected void UpdateBeamLength()
	{
		// Safety check
		if (!GetGame())
			return;
		
		BaseWorld world = GetWorld();
		if (!world)
			return;
		
		// Get world transform
		vector mat[4];
		GetWorldTransform(mat);
		
		vector origin = mat[3];
		vector direction = mat[2]; // Z axis (forward in Enfusion)
		
		// Perform raycast
		autoptr TraceParam trace = new TraceParam();
		trace.Start = origin;
		trace.End = origin + direction * m_fMaxRange;
		trace.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
		trace.LayerMask = EPhysicsLayerPresets.Projectile;
		
		// Exclude parent weapon/character
		IEntity parent = GetParent();
		if (parent)
		{
			IEntity root = parent.GetRootParent();
			if (root)
				trace.Exclude = root;
			else
				trace.Exclude = parent;
		}
		
		float hitFraction = world.TraceMove(trace, null);
		
		if (hitFraction < 1.0)
		{
			m_fCurrentLength = m_fMaxRange * hitFraction;
			m_vHitPoint = origin + direction * m_fCurrentLength;
			m_bHitSomething = true;
		}
		else
		{
			m_fCurrentLength = m_fMaxRange;
			m_vHitPoint = origin + direction * m_fMaxRange;
			m_bHitSomething = false;
		}
		
		// Scale the beam mesh to match length
		if (m_fMeshDefaultLength > 0)
		{
			float scaleLen = m_fCurrentLength / m_fMeshDefaultLength;
			
			if (m_BeamMeshEntity)
			{
				// Scale Z axis for length, X/Y for thickness
				vector scaleMat[4];
				Math3D.MatrixIdentity4(scaleMat);
				
				// X and Y use thickness setting (1.0 = original mesh thickness)
				scaleMat[0] = Vector(m_fBeamThickness, 0, 0);
				scaleMat[1] = Vector(0, m_fBeamThickness, 0);
				// Z axis = beam length direction
				scaleMat[2] = Vector(0, 0, scaleLen);
				scaleMat[3] = Vector(0, 0, 0);
				
				m_BeamMeshEntity.SetLocalTransform(scaleMat);
			}
			else
			{
				SetScale(scaleLen);
			}
		}
	}
	
	//------------------------------------------------------------------------------------------------
	// THE ONLY METHOD THAT DETERMINES IF LASER SHOULD BE VISIBLE
	//------------------------------------------------------------------------------------------------
	protected bool ShouldBeVisible()
	{
		// Laser visible ONLY if:
		// 1. IR is enabled (user turned it on) AND
		// 2. (NVG is active OR IR filter is removed)
		return m_bIREnabled && (m_bNVGActive || m_bIRFilterRemoved);
	}
	
	//------------------------------------------------------------------------------------------------
	// Force visibility update - called when state changes
	//------------------------------------------------------------------------------------------------
	protected void ForceUpdateVisibility()
	{
		bool visible = ShouldBeVisible();
		
		
		if (m_BeamMeshEntity)
		{
			if (visible)
			{
				m_BeamMeshEntity.SetFlags(EntityFlags.VISIBLE | EntityFlags.ACTIVE);
				// Removed verbose logging - only log warnings/errors
			}
			else
			{
				m_BeamMeshEntity.ClearFlags(EntityFlags.VISIBLE | EntityFlags.ACTIVE);
				// Removed verbose logging - only log warnings/errors
			}
		}
		else
		{
			// No spawned mesh - control this entity directly (mesh is attached to prefab)
			if (visible)
			{
				SetFlags(EntityFlags.VISIBLE | EntityFlags.ACTIVE);
				SetScale(1.0);
				// Removed verbose logging - only log warnings/errors
			}
			else
			{
				ClearFlags(EntityFlags.VISIBLE | EntityFlags.ACTIVE);
				SetScale(0.0001);
				// Removed verbose logging - only log warnings/errors
			}
		}
		
		// FPS patch: only run EOnFrame while visible. State changes call ForceUpdateVisibility(),
		// so NVG/IR toggles can re-enable FRAME when needed.
		if (visible)
			SetEventMask(EntityEvent.INIT | EntityEvent.FRAME);
		else
			SetEventMask(EntityEvent.INIT);
	}
	
	//------------------------------------------------------------------------------------------------
	// Apply beam color based on IR filter state
	//------------------------------------------------------------------------------------------------
	protected void ApplyBeamColor()
	{
		
		if (!m_BeamMaterial)
		{
			FindAndSetupMaterial();
			if (!m_BeamMaterial)
			{
				return;
			}
		}
		
		vector color;
		if (m_bIRFilterRemoved)
		{
			// Visible mode - use visible color (red by default)
			color = m_vVisibleColor;
		}
		else
		{
			// IR mode - use IR color (greenish-white by default)
			color = m_vIRColor;
		}
		
		// Apply to material
		// For additive blend mode, opacity controls brightness/visibility
		Color c = Color.FromVector(color);
		int packedColor = c.PackToInt();
		
		// Set color properties
		m_BeamMaterial.SetColor(packedColor);
		m_BeamMaterial.SetEmissiveColor(packedColor);
		
		// Use opacity to scale the emissive multiplier (controls beam visibility)
		float effectiveIntensity = m_fIntensity * m_fBeamOpacity;
		m_BeamMaterial.SetEmissiveMultiplier(effectiveIntensity);
		
		// Removed verbose logging - only log warnings/errors
	}
	
	//------------------------------------------------------------------------------------------------
	// Public API - ONLY these methods should change visibility state
	//------------------------------------------------------------------------------------------------
	
	void SetIREnabled(bool enabled)
	{
		// Removed verbose logging - only log warnings/errors
		m_bIREnabled = enabled;
		ForceUpdateVisibility();
	}
	
	void SetIREnabledWithNVGState(bool enabled, bool nvgActive)
	{
		m_bIREnabled = enabled;
		m_bNVGActive = nvgActive;
		// Removed verbose logging - only log warnings/errors
		ForceUpdateVisibility();
	}
	
	bool IsIREnabled()
	{
		return m_bIREnabled;
	}
	
	void OnNVGStateChanged(bool nvgActive)
	{
		// Removed verbose logging - only log warnings/errors
		m_bNVGActive = nvgActive;
		ForceUpdateVisibility();
	}
	
	void SetIRFilterRemoved(bool removed)
	{
		// Removed verbose logging - only log warnings/errors
		m_bIRFilterRemoved = removed;
		ApplyBeamColor();
		ForceUpdateVisibility();
	}
	
	bool IsIRFilterRemoved()
	{
		return m_bIRFilterRemoved;
	}
	
	// Set custom color for the beam (when filter is removed)
	void SetVisibleColor(vector color)
	{
		// Removed verbose logging - only log warnings/errors
		m_vVisibleColor = color;
		if (m_bIRFilterRemoved)
			ApplyBeamColor();
	}

	// Set custom color for the IR beam
	void SetIRColor(vector color)
	{
		m_vIRColor = color;
		if (!m_bIRFilterRemoved)
			ApplyBeamColor();
	}
	
	// Set beam opacity (0 = transparent, 1 = solid)
	void SetBeamOpacity(float opacity)
	{
		m_fBeamOpacity = Math.Clamp(opacity, 0.01, 1.0);
		ApplyBeamColor();
	}
	
	float GetBeamOpacity()
	{
		return m_fBeamOpacity;
	}
	
	// Set beam intensity
	void SetBeamIntensity(float intensity)
	{
		m_fIntensity = Math.Clamp(intensity, 1, 500);
		ApplyBeamColor();
	}
	
	float GetBeamIntensity()
	{
		return m_fIntensity;
	}
	
	void ToggleIRFilter()
	{
		SetIRFilterRemoved(!m_bIRFilterRemoved);
	}
	
	// Legacy compatibility
	void SetIRState(bool active)
	{
		SetIREnabled(active);
	}
	
	//------------------------------------------------------------------------------------------------
	// Getters
	//------------------------------------------------------------------------------------------------
	
	vector GetHitPoint()
	{
		return m_vHitPoint;
	}
	
	float GetCurrentLength()
	{
		return m_fCurrentLength;
	}
	
	bool DidHitSomething()
	{
		return m_bHitSomething;
	}
	
	//------------------------------------------------------------------------------------------------
	void ~ACM_IRLaser()
	{
		UnregisterFromIRSystem();
		
		// Remove pending callqueues
		if (GetGame() && GetGame().GetCallqueue())
		{
			GetGame().GetCallqueue().Remove(DeferredInit);
			GetGame().GetCallqueue().Remove(RegisterWithIRSystem);
			GetGame().GetCallqueue().Remove(FindAndSetupMaterial);
		}
		
		// Clean up child mesh entity - use SCR_EntityHelper instead of delete
		if (m_BeamMeshEntity)
		{
			SCR_EntityHelper.DeleteEntityAndChildren(m_BeamMeshEntity);
			m_BeamMeshEntity = null;
		}
	}
}
