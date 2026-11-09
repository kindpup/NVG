//------------------------------------------------------------------------------------------------
// ACM_NVL_EquipmentLightsComponent.c
// Light component for character-worn equipment: helmets, straps, body attachments.
//
// Equipment items are scene-parented directly to the character entity when worn.
// Ownership is resolved via a simple parent-walk – no weapon slot-chain traversal
// and no PlayerManager scan are needed.  This keeps the readiness path fast and
// free of the DS-side noise that weapon attachments produce.
//
// CheckReadiness flow (inherited from ACM_NVL_LightsComponent base):
//   RS_LOCAL_INIT  → RS_CHARACTER_RESOLVED (slot gate skipped; IsOnWeapon()=false)
//               → RS_NETWORK_RESOLVED → RS_FULLY_READY
//
// Part of ACM Night Vision and Lights mod
//------------------------------------------------------------------------------------------------

[ComponentEditorProps(category: "ACM/Lights", description: "Light component for character-worn equipment (helmets, body attachments). Simple parent-walk ownership, no weapon slot resolution.")]
class ACM_NVL_EquipmentLightsComponentClass : ACM_NVL_LightsComponentClass {}

class ACM_NVL_EquipmentLightsComponent : ACM_NVL_LightsComponent
{
	// Equipment items are never mounted on weapons.
	// Returning false here causes the base CheckReadiness to skip
	// the slot-linkage gate (Step RS_LOCAL_INIT → RS_ATTACHED_NO_CHARACTER)
	// and proceed directly to character resolution, and prevents the
	// weapon-manager path in GetCharacterOwner() from being taken.
	override protected bool IsOnWeapon()
	{
		return false;
	}

	// Use an equipment-specific tag so log lines from worn items
	// are trivially distinguishable from weapon-attachment readiness events.
	override protected string GetReadinessLogTag()
	{
		return "ACM_EquipReady";
	}
}
