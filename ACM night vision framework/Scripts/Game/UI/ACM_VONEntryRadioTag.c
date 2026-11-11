//
//
//  ACM
//  ACM VON Radial Menu Radio Tags
//
//  Shows a net label on each radio entry of the VON radial menu so players can tell at a
//  glance which net a given channel serves.
//
//  HOW IT WORKS (based on vanilla SCR_VONEntryRadio internals):
//  the entry widget's channel slot is rendered from m_sChannelText on every Update(), and
//  every code path that changes that field — InitEntry, radial-menu tuning, menu refresh —
//  funnels through SetChannelText(). Overriding that single method makes the ACM tag win
//  the channel slot permanently; the frequency readout (m_sText) is untouched.
//
//  Label priority per entry (the menu shows ONE ENTRY PER TRANSCEIVER):
//    1. Channel Label on the transceiver itself — ACM_LabeledRelayTransceiver (standard
//       ACM relay channel) or ACM_LabeledTransceiver (tower-gated channel).
//    2. Legacy per-channel label list on the radio's SCR_CoverageRadioComponent.
//    3. Radio fallback tag — "ACM Radio" settings on SCR_RadioComponent
//       (custom tag, else tier name: INTER-SQUAD / SQUAD / JTAC / MANPACK).
//  Radios with no ACM tag (vanilla/other mods) keep vanilla channel text unchanged.
//==================================================================================================

modded class SCR_VONEntryRadio : SCR_VONEntry
{
	//! Single gateway into m_sChannelText — the field the entry widget renders from.
	//! When this radio has an ACM tag, the tag always wins the channel slot.
	override void SetChannelText(string channel)
	{
		string tag = ACM_ResolveChannelTag();
		if (!tag.IsEmpty())
			super.SetChannelText(tag);
		else
			super.SetChannelText(channel);
	}

	protected string ACM_ResolveChannelTag()
	{
		string tag;

		BaseTransceiver transceiver = GetTransceiver();

		// 1) Label authored directly on the transceiver (relay or plain variant).
		ACM_LabeledRelayTransceiver labeledRelay = ACM_LabeledRelayTransceiver.Cast(transceiver);
		if (labeledRelay)
			tag = labeledRelay.m_sACM_ChannelLabel;

		if (tag.IsEmpty())
		{
			ACM_LabeledTransceiver labeledTransceiver = ACM_LabeledTransceiver.Cast(transceiver);
			if (labeledTransceiver)
				tag = labeledTransceiver.m_sACM_ChannelLabel;
		}

		// 2) Legacy index-matched label list on the coverage component.
		if (tag.IsEmpty() && transceiver)
		{
			SCR_CoverageRadioComponent coverageRadio = SCR_CoverageRadioComponent.Cast(transceiver.GetRadio());
			if (coverageRadio)
				tag = coverageRadio.ACM_GetChannelLabel(GetTransceiverNumber() - 1); // number starts at 1
		}

		// 3) Radio-level fallback tag (custom string or tier name).
		if (tag.IsEmpty())
		{
			SCR_RadioComponent radioComponent = SCR_RadioComponent.Cast(GetGadget());
			if (radioComponent)
				tag = radioComponent.ACM_GetRadioTag();
		}

		return tag;
	}
}

