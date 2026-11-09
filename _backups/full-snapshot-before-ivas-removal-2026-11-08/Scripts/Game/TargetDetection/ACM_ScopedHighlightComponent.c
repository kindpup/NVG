// ACM Scoped Highlight
//
// THE PROBLEM THIS EXISTS TO SOLVE.
//
// World.OutlineEntity is documented as outlining an entity "in the current frame", and that is
// exactly what it does: it is a main-view pass. Looking through a 2D PIP optic, the glass shows a
// DIFFERENT camera's render composited into a circle, so an outline drawn in the main view lands
// where the body sits UNMAGNIFIED while the body the wearer can actually see sits somewhere else
// inside the circle. The two images are not the same image. No coordinate mapping reconciles them,
// and every attempt to map screen space into the glass made the placement worse rather than better.
//
// A material change does not have this problem. It changes the MODEL, not the frame, so it is
// present in every camera that renders the model - the main view, the scope render target, and the
// IVAS remote feed alike - with no projection maths anywhere. That is the whole idea here.
//
// A CHARACTER IS NOT ONE MESH.
//
// The first version of this drove the ParametricMaterialInstanceComponent on the character root and
// reported complete success while changing nothing anybody could see. The log said why: the root
// entity's mesh is Assets/Characters/Basebody/Basebody_Male_01.xob - the naked body. Uniform, vest,
// helmet, webbing and rifle are all SEPARATE CHILD ENTITIES with their own materials, and they
// cover the thing being lit almost completely.
//
// So this walks the hierarchy and drives every parametric material it can reach, root included.
// A soldier is lit because his clothes are lit.
//
// AND THE PROBLEM IT CREATES.
//
// OutlineEntity is per-frame and self-clearing: stop calling it and the outline is simply gone next
// frame. A material change is STICKY. Set it and walk away and the target stays lit - through death,
// through the goggles coming off, through the IVAS being switched off, forever. That failure is
// worse than the bug being fixed, because a permanently glowing enemy is both a visual defect and
// an unearned advantage.
//
// So the lifetime is the real work here, and it is defended four separate ways rather than once:
//
//   1. Every Light() call carries an expiry. The draw pass refreshes it each time round, so a
//      contact that stops being drawn goes dark on its own within a fraction of a second without
//      anybody having to remember to clear it.
//   2. A single shared sweep timer on the callqueue extinguishes anything past its expiry. One
//      timer for all lit entities, not one per entity, and it stops itself when nothing is lit.
//      The callqueue keeps running through menus - that was established the hard way - so this net
//      holds even while the draw pass is idle.
//   3. ExtinguishAll() is called from the detection system's own teardown paths, so a hard stop
//      does not wait on a timer.
//   4. OnDelete restores the material when the character itself goes away, which matters because
//      an entity can be streamed out or destroyed while lit.
//
// Any one of the four is sufficient. All four are cheap.
//
// CLIENT-LOCAL BY CONSTRUCTION.
//
// Nothing here is replicated. The component is driven only by the local wearer's draw pass, so a
// contact lit on this machine is lit on this machine alone. Other players see an ordinary soldier.
// That is a requirement, not a happy accident: a highlight that replicated would hand every player
// in the session a wallhack sourced from one player's optic.

class ACM_ScopedHighlightComponentClass : ScriptComponentClass
{
}

//------------------------------------------------------------------------------------------------
// One material instance we have taken over, and what it looked like before we did.
//
// Restored verbatim rather than reset to a neutral value, so a character that was already tinted or
// emissive for its own reasons - a damage effect, a faction recolour - gets its own look back.
//------------------------------------------------------------------------------------------------
class ACM_HighlightMaterial
{
	ParametricMaterialInstanceComponent material;

	int savedColor;
	int savedEmissiveColor;
	int savedEmissiveMultiplier;

	void ACM_HighlightMaterial(ParametricMaterialInstanceComponent comp)
	{
		material = comp;
		savedColor = comp.GetColor();
		savedEmissiveColor = comp.GetEmissiveC();
		savedEmissiveMultiplier = comp.GetEmissiveM();
	}
}

//------------------------------------------------------------------------------------------------
// Owns the sweep timer and the register of what is currently lit.
//
// A manager rather than statics on the component itself because the callqueue wants a bound method
// to call, and because one timer shared by every lit entity is the difference between one repeating
// call and thirty-two of them.
//------------------------------------------------------------------------------------------------
class ACM_ScopedHighlightManager
{
	protected static ref ACM_ScopedHighlightManager s_Instance;

	protected ref array<ACM_ScopedHighlightComponent> m_aLit;
	protected bool m_bSweepRunning;

	protected static const int SWEEP_INTERVAL_MS = 200;

	//------------------------------------------------------------------------------------------------
	static ACM_ScopedHighlightManager GetInstance()
	{
		if (!s_Instance)
			s_Instance = new ACM_ScopedHighlightManager();

		return s_Instance;
	}

	//------------------------------------------------------------------------------------------------
	void ACM_ScopedHighlightManager()
	{
		m_aLit = new array<ACM_ScopedHighlightComponent>();
	}

	//------------------------------------------------------------------------------------------------
	void Register(ACM_ScopedHighlightComponent comp)
	{
		if (!comp)
			return;

		if (m_aLit.Find(comp) == -1)
			m_aLit.Insert(comp);

		StartSweep();
	}

	//------------------------------------------------------------------------------------------------
	void Unregister(ACM_ScopedHighlightComponent comp)
	{
		int index = m_aLit.Find(comp);
		if (index != -1)
			m_aLit.Remove(index);

		if (m_aLit.IsEmpty())
			StopSweep();
	}

	//------------------------------------------------------------------------------------------------
	// Walks backwards so removing while iterating is safe - Extinguish() calls Unregister().
	//------------------------------------------------------------------------------------------------
	void Sweep()
	{
		float now = System.GetTickCount() / 1000.0;

		for (int i = m_aLit.Count() - 1; i >= 0; i--)
		{
			ACM_ScopedHighlightComponent comp = m_aLit[i];

			if (!comp)
			{
				m_aLit.Remove(i);
				continue;
			}

			if (comp.IsExpired(now))
				comp.Extinguish();
		}

		if (m_aLit.IsEmpty())
			StopSweep();
	}

	//------------------------------------------------------------------------------------------------
	// The hard stop. Called from the detection system's teardown so nothing waits on a timer.
	//------------------------------------------------------------------------------------------------
	void ExtinguishAll()
	{
		for (int i = m_aLit.Count() - 1; i >= 0; i--)
		{
			ACM_ScopedHighlightComponent comp = m_aLit[i];

			if (comp)
				comp.Extinguish();
			else
				m_aLit.Remove(i);
		}

		m_aLit.Clear();
		StopSweep();
	}

	//------------------------------------------------------------------------------------------------
	protected void StartSweep()
	{
		if (m_bSweepRunning)
			return;

		if (!GetGame() || !GetGame().GetCallqueue())
			return;

		GetGame().GetCallqueue().Remove(Sweep);
		GetGame().GetCallqueue().CallLater(Sweep, SWEEP_INTERVAL_MS, true);
		m_bSweepRunning = true;
	}

	//------------------------------------------------------------------------------------------------
	protected void StopSweep()
	{
		if (!m_bSweepRunning)
			return;

		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().Remove(Sweep);

		m_bSweepRunning = false;
	}
}

//------------------------------------------------------------------------------------------------
// Goes on the base character prefab. Every character inherits it; nothing else needs to know it is
// there. It holds no opinion about who deserves highlighting - that judgement belongs entirely to
// the detection system, which already owns classification, line of sight and the wearer's settings.
// This is the actuator, not the decision.
//------------------------------------------------------------------------------------------------
class ACM_ScopedHighlightComponent : ScriptComponent
{
	[Attribute("1", UIWidgets.CheckBox, desc: "Allow this character to be highlighted by an IVAS wearer looking through a magnified optic. Off makes the character permanently unhighlightable, which is useful for anything that should never read as a target.", category: "ACM Scoped Highlight")]
	protected bool m_bHighlightable;

	[Attribute("1", UIWidgets.CheckBox, desc: "TEMPORARY TEST. Tint the albedo as well as the emissive. Albedo always renders, so if the character changes colour with this on and does nothing with it off, the material pipeline works and only the emissive is being ignored.", category: "ACM Scoped Highlight")]
	protected bool m_bTintAlbedo;

	[Attribute("3", UIWidgets.EditBox, desc: "How deep into the character hierarchy to look for materials. Uniform and vest sit one level down; anything attached to those sits two.", category: "ACM Scoped Highlight")]
	protected int m_iMaxDepth;

	protected ref array<ref ACM_HighlightMaterial> m_aMaterials;
	protected bool m_bResolved;

	protected bool m_bLit;
	protected float m_fExpiry;

	//------------------------------------------------------------------------------------------------
	// How many parametric materials this character actually exposes. Reported by the detection
	// system's probe: a count of 1 means we found only the naked base body and the clothing is not
	// reachable, which looks exactly like "the highlight does not work" from inside the game.
	//------------------------------------------------------------------------------------------------
	int GetMaterialCount()
	{
		ResolveMaterials();

		if (!m_aMaterials)
			return 0;

		return m_aMaterials.Count();
	}

	//------------------------------------------------------------------------------------------------
	bool CanHighlight()
	{
		return m_bHighlightable && GetMaterialCount() > 0;
	}

	//------------------------------------------------------------------------------------------------
	// Resolved once, lazily, on first use. A character is assembled over several frames and its
	// loadout is attached long after OnPostInit, so resolving early finds a naked body and nothing
	// else.
	//------------------------------------------------------------------------------------------------
	protected bool ResolveMaterials()
	{
		if (m_bResolved)
			return !m_aMaterials.IsEmpty();

		m_aMaterials = new array<ref ACM_HighlightMaterial>();
		m_bResolved = true;

		IEntity owner = GetOwner();
		if (!owner)
			return false;

		CollectMaterials(owner, 0);

		return !m_aMaterials.IsEmpty();
	}

	//------------------------------------------------------------------------------------------------
	// Depth-first walk of the entity and everything hanging off it.
	//------------------------------------------------------------------------------------------------
	protected void CollectMaterials(IEntity entity, int depth)
	{
		if (!entity || depth > m_iMaxDepth)
			return;

		ParametricMaterialInstanceComponent comp = ParametricMaterialInstanceComponent.Cast(entity.FindComponent(ParametricMaterialInstanceComponent));
		if (comp)
			m_aMaterials.Insert(new ACM_HighlightMaterial(comp));

		IEntity child = entity.GetChildren();
		while (child)
		{
			CollectMaterials(child, depth + 1);
			child = child.GetSibling();
		}
	}

	//------------------------------------------------------------------------------------------------
	// Light this character, and keep it lit for holdSeconds unless refreshed.
	//
	// Called every draw pass for as long as the contact should glow. Refreshing the expiry is the
	// primary lifetime mechanism: the draw pass does not have to remember to turn anything off, it
	// only has to stop asking.
	//
	// Returns whether the highlight is actually on the character, so the caller can fall back to a
	// canvas mark when it is not.
	//------------------------------------------------------------------------------------------------
	bool Light(int argbColor, int multiplier, float holdSeconds)
	{
		if (!m_bHighlightable || !ResolveMaterials())
			return false;

		if (!m_bLit)
		{
			m_bLit = true;
			ACM_ScopedHighlightManager.GetInstance().Register(this);
		}

		foreach (ACM_HighlightMaterial entry : m_aMaterials)
		{
			if (!entry || !entry.material)
				continue;

			entry.material.SetEmissiveColor(argbColor);
			entry.material.SetEmissiveMultiplier(multiplier);

			if (m_bTintAlbedo)
				entry.material.SetColor(argbColor);
		}

		m_fExpiry = (System.GetTickCount() / 1000.0) + holdSeconds;
		return true;
	}

	//------------------------------------------------------------------------------------------------
	bool IsExpired(float now)
	{
		return m_bLit && now >= m_fExpiry;
	}

	//------------------------------------------------------------------------------------------------
	void Extinguish()
	{
		if (!m_bLit)
			return;

		m_bLit = false;

		if (m_aMaterials)
		{
			foreach (ACM_HighlightMaterial entry : m_aMaterials)
			{
				if (!entry || !entry.material)
					continue;

				entry.material.SetColor(entry.savedColor);
				entry.material.SetEmissiveColor(entry.savedEmissiveColor);
				entry.material.SetEmissiveMultiplier(entry.savedEmissiveMultiplier);
			}
		}

		ACM_ScopedHighlightManager.GetInstance().Unregister(this);
	}

	//------------------------------------------------------------------------------------------------
	// A loadout change swaps child entities, so the cached list goes stale. Cheaper and far safer to
	// re-resolve than to hold references to meshes that may already be gone.
	//------------------------------------------------------------------------------------------------
	void InvalidateMaterials()
	{
		Extinguish();

		m_bResolved = false;
		m_aMaterials = null;
	}

	//------------------------------------------------------------------------------------------------
	// The character is going away while lit. Nothing needs restoring on a mesh about to be
	// destroyed, but the manager must not be left holding a dangling entry.
	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		if (m_bLit)
		{
			m_bLit = false;
			ACM_ScopedHighlightManager.GetInstance().Unregister(this);
		}

		super.OnDelete(owner);
	}
}
