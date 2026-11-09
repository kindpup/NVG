//------------------------------------------------------------------------------------------------
// ACM_NVGFaceGlow.c
// The phosphor glow that leaks out of the eyecups onto the wearer's face: two small, soft point
// lights just in front of the eyes, in the colour of the active phosphor. Purely visual and local to
// each machine; the on/off state and colour reach other players through ACM_PlayerLinkComponent.
//
// The lights are NOT parented to a bone: their position is computed every frame from the head bone's
// real world transform, so placement does not depend on how the engine frames a bone attachment.
//
// Changing colour flashes the eyecups briefly before settling on the new colour.
//------------------------------------------------------------------------------------------------
class ACM_NVGFaceGlow : Managed
{
	protected static const string HEAD_BONE = "Head";

	// Eye position relative to the head joint (top of the neck), metres along the head's forward / up.
	protected static const float EYE_FORWARD = 0.09;
	protected static const float EYE_UP = 0.065;
	// Lights sit this far in front of the eyes (where the eyecups are) ...
	protected static const float CUP_GAP = 0.015;
	// ... and slightly inside the eye spacing, so the two overlap on the bridge of the nose.
	protected static const float EYE_SIDE = 0.026;

	// Small, short-reach lights: eye sockets and nose bridge only.
	protected static const float GLOW_RADIUS = 0.12;
	protected static const float GLOW_LV = 3.2;

	// Colour-change flash.
	protected static const float FLASH_LV = 4.5;
	protected static const int FLASH_MS = 140;

	// One position report per session, for tuning.
	protected static bool s_bPlacementLogged;

	protected IEntity m_Character;
	protected ref array<LightEntity> m_aLights = {};
	protected bool m_bOn;
	protected vector m_vColor;
	protected bool m_bFlashing;
	protected bool m_bTicking;
	protected TNodeId m_iHeadBone = -1;

	//------------------------------------------------------------------------------------------------
	void ACM_NVGFaceGlow(IEntity character)
	{
		m_Character = character;
	}

	//------------------------------------------------------------------------------------------------
	void ~ACM_NVGFaceGlow()
	{
		DestroyLights();
	}

	//------------------------------------------------------------------------------------------------
	//! Turn the glow on in the given colour (RGB 0-1), or off. A colour change while on flashes first.
	void Apply(bool on, vector color)
	{
		if (!on)
		{
			m_bOn = false;
			DestroyLights();
			return;
		}

		bool colorChanged = m_bOn && color != m_vColor;
		m_vColor = color;
		m_bOn = true;

		if (m_aLights.IsEmpty())
			CreateLights();

		if (colorChanged)
			Flash();
		else if (!m_bFlashing)
			SetLightColor(m_vColor, GLOW_LV);

		if (!m_bTicking)
		{
			m_bTicking = true;
			GetGame().GetCallqueue().CallLater(Tick, 0, true);
		}
	}

	//------------------------------------------------------------------------------------------------
	bool IsOn()
	{
		return m_bOn;
	}

	//------------------------------------------------------------------------------------------------
	//! A short burst of the "other" colour (the complement of the new one, brightened), then settle.
	protected void Flash()
	{
		vector flashColor = Vector(1.0 - m_vColor[0] * 0.6, 1.0 - m_vColor[1] * 0.6, 1.0 - m_vColor[2] * 0.6);
		SetLightColor(flashColor, FLASH_LV);

		m_bFlashing = true;
		GetGame().GetCallqueue().Remove(EndFlash);
		GetGame().GetCallqueue().CallLater(EndFlash, FLASH_MS, false);
	}

	//------------------------------------------------------------------------------------------------
	protected void EndFlash()
	{
		m_bFlashing = false;
		if (m_bOn)
			SetLightColor(m_vColor, GLOW_LV);
	}

	//------------------------------------------------------------------------------------------------
	protected void SetLightColor(vector color, float lv)
	{
		foreach (LightEntity light : m_aLights)
		{
			if (light)
				light.SetColor(Color.FromVector(color), lv);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void CreateLights()
	{
		if (!m_Character)
			return;

		for (int i = 0; i < 2; i++)
		{
			LightEntity light = LightEntity.CreateLight(LightType.POINT, LightFlags.DIFFUSE_ONLY, GLOW_RADIUS, Color.FromVector(m_vColor), GLOW_LV, m_Character.GetOrigin());
			if (!light)
				continue;

			light.SetCastShadow(false);
			// No glare sprite: it should light the face, not appear as a glowing ball.
			light.SetLensFlareType(LightLensFlareType.Disabled);
			m_aLights.Insert(light);
		}

		Tick();
	}

	//------------------------------------------------------------------------------------------------
	//! Every frame while on: put each light in front of its eye, from the head bone's world transform.
	protected void Tick()
	{
		if (!m_bOn || !m_Character || m_aLights.Count() < 2)
		{
			if (!m_Character)
				Apply(false, vector.Zero);
			return;
		}

		Animation anim = m_Character.GetAnimation();
		if (!anim)
			return;

		if (m_iHeadBone < 0)
			m_iHeadBone = anim.GetBoneIndex(HEAD_BONE);
		if (m_iHeadBone < 0)
			return; // skeleton not ready yet; try again next frame

		vector bone[4];
		if (!anim.GetBoneMatrix(m_iHeadBone, bone))
			return;

		vector world[4];
		m_Character.GetWorldTransform(world);

		// Head joint and the head's own axes, in world space (bone matrix is in model space).
		vector headPos = ToWorldDir(bone[3], world) + world[3];
		vector axis0 = ToWorldDir(bone[0], world);
		vector axis1 = ToWorldDir(bone[1], world);
		vector axis2 = ToWorldDir(bone[2], world);

		// Which of the head's axes are forward and up is not assumed: take the ones closest to the
		// character's own forward and up.
		vector fwd = Closest(axis0, axis1, axis2, world[2]);
		vector up = Closest(axis0, axis1, axis2, world[1]);
		vector right = up * fwd;
		right.Normalize();
		if (vector.Dot(right, world[0]) < 0)
			right = right * -1;

		vector centre = headPos + fwd * (EYE_FORWARD + CUP_GAP) + up * EYE_UP;

		m_aLights[0].SetOrigin(centre - right * EYE_SIDE);
		m_aLights[1].SetOrigin(centre + right * EYE_SIDE);

		// The wearer cannot see their own face in first person; there the lights would only shine
		// into their view. Shown in third person and always to everyone else.
		bool visible = true;
		if (m_Character == SCR_PlayerController.GetLocalControlledEntity())
		{
			ChimeraCharacter chimera = ChimeraCharacter.Cast(m_Character);
			if (chimera && chimera.GetCharacterController() && !chimera.GetCharacterController().IsInThirdPersonView())
				visible = false;
		}

		foreach (LightEntity light : m_aLights)
		{
			if (light && light.IsEnabled() != visible)
				light.SetEnabled(visible);
		}

		if (!s_bPlacementLogged)
		{
			s_bPlacementLogged = true;
			vector charPos = world[3];
			Print("[ACM FaceGlow] head joint above feet=" + (headPos[1] - charPos[1]).ToString() + " lights above feet=" + (centre[1] - charPos[1]).ToString() + " lights ahead of head joint=" + vector.Dot(centre - headPos, world[2]).ToString(), LogLevel.DEBUG);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Model-space direction to world space (rotation only).
	protected vector ToWorldDir(vector v, vector world[4])
	{
		return world[0] * v[0] + world[1] * v[1] + world[2] * v[2];
	}

	//------------------------------------------------------------------------------------------------
	//! The axis (with sign) pointing most along dir.
	protected vector Closest(vector a, vector b, vector c, vector dir)
	{
		vector best = a;
		float bestDot = vector.Dot(a, dir);
		if (Math.AbsFloat(vector.Dot(b, dir)) > Math.AbsFloat(bestDot))
		{
			best = b;
			bestDot = vector.Dot(b, dir);
		}
		if (Math.AbsFloat(vector.Dot(c, dir)) > Math.AbsFloat(bestDot))
		{
			best = c;
			bestDot = vector.Dot(c, dir);
		}

		if (bestDot < 0)
			best = best * -1;

		best.Normalize();
		return best;
	}

	//------------------------------------------------------------------------------------------------
	protected void DestroyLights()
	{
		if (GetGame() && GetGame().GetCallqueue())
		{
			GetGame().GetCallqueue().Remove(EndFlash);
			GetGame().GetCallqueue().Remove(Tick);
		}
		m_bTicking = false;
		m_bFlashing = false;

		foreach (LightEntity light : m_aLights)
		{
			if (light)
				SCR_EntityHelper.DeleteEntityAndChildren(light);
		}
		m_aLights.Clear();
	}

	//------------------------------------------------------------------------------------------------
	//! Glow colour for a phosphor, from its name or material path. Black override = automatic.
	static vector ColorForPhosphor(string name, ResourceName material, vector overrideColor)
	{
		if (overrideColor != vector.Zero)
			return overrideColor;

		string key = name + " " + material;
		key.ToLower();

		if (key.Contains("green"))
			return Vector(0.25, 1.0, 0.15);
		if (key.Contains("red"))
			return Vector(1.0, 0.15, 0.1);
		if (key.Contains("blue"))
			return Vector(0.25, 0.5, 1.0);
		if (key.Contains("amber") || key.Contains("orange"))
			return Vector(1.0, 0.6, 0.1);

		// White phosphor (and anything unknown): slightly blue-white.
		return Vector(0.75, 0.9, 1.0);
	}
}
