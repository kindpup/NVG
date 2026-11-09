//------------------------------------------------------------------------------------------------
// ACM_NVL_WeaponLightComponent.c
// Weapon-mounted light component with optional visible and IR channels
//------------------------------------------------------------------------------------------------

[ComponentEditorProps(category: "ACM/Lights", description: "Weapon-mounted light component with IR/non-IR support")]
class ACM_NVL_WeaponLightComponentClass : ACM_NVL_LightsComponentClass {}

class ACM_NVL_WeaponLightComponent : ACM_NVL_LightsComponent
{
	[Attribute("1", UIWidgets.CheckBox, desc: "If enabled, weapon light listens to dedicated keybind actions. If disabled, it reuses the shared light keybinds.")]
	protected bool m_bEnableDedicatedInput;

	[Attribute("ACM_WeaponLight_Toggle", UIWidgets.EditBox, desc: "Dedicated action to toggle visible weapon light")]
	protected string m_sDedicatedToggleAction;

	[Attribute("ACM_WeaponLight_ToggleIR", UIWidgets.EditBox, desc: "Dedicated action to toggle IR weapon light")]
	protected string m_sDedicatedToggleIRAction;

	[Attribute("ACM_WeaponLight_BrightnessUp", UIWidgets.EditBox, desc: "Dedicated action for weapon-light brightness up")]
	protected string m_sDedicatedBrightnessUpAction;

	[Attribute("ACM_WeaponLight_BrightnessDown", UIWidgets.EditBox, desc: "Dedicated action for weapon-light brightness down")]
	protected string m_sDedicatedBrightnessDownAction;

	[Attribute("ACM_WeaponLight_BeamWider", UIWidgets.EditBox, desc: "Dedicated action to widen weapon-light beam cone angle")]
	protected string m_sDedicatedBeamWiderAction;

	[Attribute("ACM_WeaponLight_BeamNarrow", UIWidgets.EditBox, desc: "Dedicated action to narrow weapon-light beam cone angle")]
	protected string m_sDedicatedBeamNarrowAction;

	// Weapon-mounted lights are always on a weapon entity or weapon attachment.
	// Returning true here activates the slot-linkage gate in CheckReadiness
	// (RS_LOCAL_INIT → RS_ATTACHED_NO_CHARACTER) and uses the weapon-manager
	// path in GetCharacterOwner() for DS-safe character resolution.
	override protected bool IsOnWeapon()
	{
		return true;
	}

	// Weapon-specific log tag separates readiness events from equipment items.
	override protected string GetReadinessLogTag()
	{
		return "ACM_WeaponReady";
	}

	// OnPostInit is called for ALL entity instances including replicated ones on DS.
	// EOnInit may be skipped for attachments arriving via replication, so ApplyInputMode()
	// must be called here to guarantee action strings are set before DeferredInit fires.
	override void OnPostInit(IEntity owner)
	{
		ApplyInputMode();
		super.OnPostInit(owner);
	}

	override void EOnInit(IEntity owner)
	{
		ApplyInputMode();
		super.EOnInit(owner);
	}

	protected void ApplyInputMode()
	{
		if (m_bEnableDedicatedInput)
		{
			m_sToggleAction = m_sDedicatedToggleAction;
			m_sToggleIRAction = m_sDedicatedToggleIRAction;
			m_sBrightnessUpAction = m_sDedicatedBrightnessUpAction;
			m_sBrightnessDownAction = m_sDedicatedBrightnessDownAction;
			m_sConeAngleUpAction = m_sDedicatedBeamWiderAction;
			m_sConeAngleDownAction = m_sDedicatedBeamNarrowAction;
			return;
		}

		// Shared-key mode: reuse the same bindings as non-weapon lights.
		m_sToggleAction = "ACMToggleFlashlight";
		m_sToggleIRAction = "ACMToggleIR";
		if (m_sBrightnessUpAction.IsEmpty())
			m_sBrightnessUpAction = "ACM_Light_BrightnessUp";
		if (m_sBrightnessDownAction.IsEmpty())
			m_sBrightnessDownAction = "ACM_Light_BrightnessDown";
		if (m_sConeAngleUpAction.IsEmpty())
			m_sConeAngleUpAction = "ACM_Light_BeamWider";
		if (m_sConeAngleDownAction.IsEmpty())
			m_sConeAngleDownAction = "ACM_Light_BeamNarrow";
	}

	// Wrapper accessors for prefab/config logic.
	bool SupportsVisibleLight()
	{
		return HasLightType(ACM_NVL_LightType.FLASHLIGHT);
	}

	bool SupportsIRLight()
	{
		return HasLightType(ACM_NVL_LightType.IRLIGHT);
	}

	// Weapon lights must be FULLY_READY and locally owned before any input is processed.
	// Delegates to CanProcessUserInput() so the cached-char check is used consistently.
	override protected bool ShouldPollWeaponInput()
	{
		return CanProcessUserInput();
	}

	//------------------------------------------------------------------------------------------------
	// Weapon-strict override: brightness is applied + replicated ONLY when full sync-readiness
	// is confirmed via IsReplicationReadyForControlSync().  No local-only fallback.
	//------------------------------------------------------------------------------------------------
	override protected void AdjustBrightness(bool increase)
	{
		// Single helper replaces the previous four individual gate checks.
		// Verifies: FULLY_READY + stable id stored + NetworkComponent cached + Replication running.
		if (!IsReplicationReadyForControlSync())
			return;
		// Guard: should only be reached when already locally owned (redundant safety net).
		if (!IsLocalPlayerOwnerCached())
			return;

		// IsReplicationReadyForControlSync() guarantees m_sRegisteredNetworkId and
		// m_mNetworkComponentRef are non-empty/non-null — use them directly.
		string id = m_sRegisteredNetworkId;
		ACM_NVL_NetworkComponent netComp = m_mNetworkComponentRef;

		// Flashlight channel
		if (m_bFlashlightOn)
		{
			float stepF = GetEffectiveBrightnessStep(true);
			if (m_fCurrentFlashlightBrightness < 1.0)
				stepF = Math.Min(stepF, 0.05);
			else if (m_fCurrentFlashlightBrightness < 3.0)
				stepF = Math.Min(stepF, 0.1);
			if (!increase) stepF = -stepF;
			m_fCurrentFlashlightBrightness = Math.Clamp(
				m_fCurrentFlashlightBrightness + stepF,
				GetEffectiveMinBrightness(true), GetEffectiveMaxBrightness(true));
			ApplyFlashlightBrightness();
			netComp.AskSetFlashlightBrightness(this, m_fCurrentFlashlightBrightness);
		}

		// IR channel
		if (m_bIROn)
		{
			float stepI = GetEffectiveBrightnessStep(false);
			if (m_fCurrentIRBrightness < 0.5)
				stepI = Math.Min(stepI, 0.02);
			else if (m_fCurrentIRBrightness < 1.0)
				stepI = Math.Min(stepI, 0.05);
			if (!increase) stepI = -stepI;
			m_fCurrentIRBrightness = Math.Clamp(
				m_fCurrentIRBrightness + stepI,
				GetEffectiveMinBrightness(false), GetEffectiveMaxBrightness(false));
			ApplyIRBrightness();
			netComp.AskSetIRBrightness(this, m_fCurrentIRBrightness);
		}
	}

	//------------------------------------------------------------------------------------------------
	// Weapon-strict override: beam angle is applied + replicated ONLY when full sync-readiness
	// is confirmed via IsReplicationReadyForControlSync().  No local-only fallback.
	//------------------------------------------------------------------------------------------------
	override protected void AdjustConeAngle(bool widen)
	{
		if (!IsReplicationReadyForControlSync())
			return;
		// Guard: should only be reached when already locally owned (redundant safety net).
		if (!IsLocalPlayerOwnerCached())
			return;

		// IsReplicationReadyForControlSync() guarantees m_sRegisteredNetworkId and
		// m_mNetworkComponentRef are non-empty/non-null — use them directly.
		string id = m_sRegisteredNetworkId;
		ACM_NVL_NetworkComponent netComp = m_mNetworkComponentRef;

		// All gates passed: apply locally once, then send RPC.
		int step = GetEffectiveConeStep();
		if (!widen) step = -step;
		SetConeAngle(Math.ClampInt(m_iCurrentConeAngle + step, GetEffectiveMinConeAngle(), GetEffectiveMaxConeAngle()));

		netComp.AskSetBeamAngle(this, m_iCurrentConeAngle);
	}
}
