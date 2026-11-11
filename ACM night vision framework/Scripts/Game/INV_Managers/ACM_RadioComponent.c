//
//  ACM
//  ACM Radio Component
//
//  Original ACM-owned script for the modular framework.
//==================================================================================================

// Radio role tier. Drives the fallback tag in the VON radial menu.
// Tier range convention for authoring channel ranges on prefabs:
//   INTERSQUAD 400 m | SQUAD 800 m | JTAC 1200 m | MANPACK 2000 m.
enum ACM_ERadioTier
{
	INTERSQUAD = 0,
	SQUAD,
	JTAC,
	MANPACK
}

//==================================================================================================
// ARMA 1.8 — how ACM radios broadcast without relay towers (verified in testing):
//
//   Plain RadioTransceiver channels are TOWER-GATED by the 1.8 engine — they only carry VON
//   while inside relay/antenna coverage. RelayTransceiver channels carry VON on their own,
//   with their "Transmitting Range" as the reach.
//
//   Therefore ACM radio channels are authored as ACM_LabeledRelayTransceiver: a relay
//   transceiver with a per-channel VON-menu label. Channel range = tier range
//   (400/800/1200/2000). The script-side SCR_RadioCoverageSystem registration approach was
//   tried and does NOT influence the VON gate (it drives map/Conflict coverage) — do not
//   re-add it; the relay channels are the mechanism.
//==================================================================================================

//! Labeled RELAY transceiver — the standard ACM radio channel class. Carries VON without
//! tower coverage (Arma 1.8) and shows its net label in the VON radial menu.
[BaseContainerProps()]
class ACM_LabeledRelayTransceiver : RelayTransceiver
{
	[Attribute(defvalue: "", desc: "Net label shown in the VON radial menu for this channel (e.g. JTAC, SQUAD, PLATOON, CMD). Empty = radio's tier tag.")]
	string m_sACM_ChannelLabel;
}

//! Labeled plain transceiver — kept for channels that SHOULD be tower-gated
//! (long-haul nets that realistically need infrastructure). Same label behavior.
[BaseContainerProps()]
class ACM_LabeledTransceiver : RadioTransceiver
{
	[Attribute(defvalue: "", desc: "Net label shown in the VON radial menu for this channel (e.g. JTAC, SQUAD, PLATOON, CMD). Empty = radio's tier tag. NOTE: plain RadioTransceiver channels are tower-gated in 1.8; use ACM_LabeledRelayTransceiver for tower-free channels.")]
	string m_sACM_ChannelLabel;
}

//==================================================================================================
// Legacy per-channel labels on the coverage component (index-matched list). Fallback only;
// preferred authoring is the label directly on each transceiver.
//==================================================================================================
modded class SCR_CoverageRadioComponent : ScriptedRadioComponent
{
	[Attribute(desc: "LEGACY: ACM channel labels index-matched to the Transceivers list. Prefer the Channel Label field on ACM_LabeledRelayTransceiver / ACM_LabeledTransceiver instead.", category: "ACM Radio")]
	protected ref array<string> m_aACM_ChannelLabels;

	//! Label for the given transceiver index (0-based). Empty when unset.
	string ACM_GetChannelLabel(int transceiverIndex)
	{
		if (!m_aACM_ChannelLabels || transceiverIndex < 0 || transceiverIndex >= m_aACM_ChannelLabels.Count())
			return string.Empty;

		return m_aACM_ChannelLabels[transceiverIndex];
	}
}

modded class SCR_RadioComponent : SCR_GadgetComponent
{
	// ============================================================================================
	// ARMA 1.8 FIX — radios in worn ACM pouches: seated AND visible.
	//
	// Two gadget-system behaviors fight ACM pouch display:
	//   1. IsVisibleEquipped()=true lets the character gadget display re-hang the radio at
	//      the gadget anchor — the "floating above the pouch" bug. Keep it FALSE.
	//   2. With it false, the gadget system HIDES the radio entity when a picked-up radio is
	//      stowed (UpdateVisibility on the show/hide path) — the "invisible in pouch" bug.
	//      Pouch-default spawned radios never pass through the gadget lifecycle, which is
	//      why only handled radios vanished.
	//
	// Fix: keep IsVisibleEquipped false, and intercept the gadget visibility path — while
	// the radio sits in an ACM-managed slot, refuse the hide and force the entity visible.
	// Radios stored anywhere else (backpacks, vanilla storage) keep vanilla behavior.
	// ============================================================================================
	override bool IsVisibleEquipped()
	{
		return false;
	}

	//! Gadget show/hide entry point — vanilla hides the stowed radio entity here.
	override void UpdateVisibility(EGadgetMode mode)
	{
		if (ACM_IsStoredInACMManagedSlot())
		{
			ACM_StoredItemLifecycleService.ForceStoredItemVisible(GetOwner());
			return; // Do not let vanilla hide a pouch-displayed radio.
		}

		super.UpdateVisibility(mode);
	}

	override void OnModeChanged(EGadgetMode mode, IEntity charOwner)
	{
		super.OnModeChanged(mode, charOwner);
		ACM_ReassertPouchVisibility();
	}

	override void OnParentSlotChanged(InventoryStorageSlot oldSlot, InventoryStorageSlot newSlot)
	{
		super.OnParentSlotChanged(oldSlot, newSlot);
		ACM_ReassertPouchVisibility();
	}

	protected void ACM_ReassertPouchVisibility()
	{
		if (!ACM_IsStoredInACMManagedSlot())
			return;

		ACM_StoredItemLifecycleService.ForceStoredItemVisible(GetOwner());

		// Late gadget transitions (stow animation end, replication) can hide after this
		// event returns — re-assert shortly after.
		if (GetGame() && GetGame().GetCallqueue())
		{
			GetGame().GetCallqueue().CallLater(ACM_ForceVisibleIfInACMSlot, 100, false);
			GetGame().GetCallqueue().CallLater(ACM_ForceVisibleIfInACMSlot, 500, false);
		}
	}

	protected void ACM_ForceVisibleIfInACMSlot()
	{
		if (ACM_IsStoredInACMManagedSlot())
			ACM_StoredItemLifecycleService.ForceStoredItemVisible(GetOwner());
	}

	protected bool ACM_IsStoredInACMManagedSlot()
	{
		IEntity owner = GetOwner();
		if (!owner)
			return false;

		InventoryItemComponent itemComponent = InventoryItemComponent.Cast(owner.FindComponent(InventoryItemComponent));
		if (!itemComponent)
			return false;

		InventoryStorageSlot parentSlot = itemComponent.GetParentSlot();
		if (!parentSlot)
			return false;

		if (ACM_PolicyDrivenStorageSlot.Cast(parentSlot))
			return true;

		if (ACM_EquipmentStorageSlot.Cast(parentSlot))
			return true;

		BaseInventoryStorageComponent parentStorage = parentSlot.GetStorage();
		if (parentStorage && ACM_EquipmentStorageComponent.Cast(parentStorage))
			return true;

		return false;
	}

	[Attribute(ACM_ERadioTier.INTERSQUAD.ToString(), UIWidgets.ComboBox,
		"ACM radio role tier. Fallback VON menu tag for channels without their own label.",
		category: "ACM Radio", enums: ParamEnumArray.FromEnum(ACM_ERadioTier))]
	protected ACM_ERadioTier m_eACM_RadioTier;

	[Attribute(defvalue: "", desc: "Custom fallback tag shown in the VON radial menu (e.g. PLATOON, CMD). Empty = tier name. Per-channel labels win over this.", category: "ACM Radio")]
	protected string m_sACM_RadioTag;

	//! Fallback tag for VON entries whose channel has no label of its own.
	string ACM_GetRadioTag()
	{
		if (!m_sACM_RadioTag.IsEmpty())
			return m_sACM_RadioTag;

		switch (m_eACM_RadioTier)
		{
			case ACM_ERadioTier.SQUAD:   return "SQUAD";
			case ACM_ERadioTier.JTAC:    return "JTAC";
			case ACM_ERadioTier.MANPACK: return "MANPACK";
		}

		return "INTER-SQUAD";
	}
}

