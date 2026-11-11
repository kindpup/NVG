//
//  ACM
//  ACM Stored Item Lifecycle Service
//
//  Original ACM-owned script for the modular framework.
//==================================================================================================

/*!
	ACM stored-item lifecycle helpers.

	ACM display slots intentionally suppress physics interaction while an item is stored
	and restore the previous interaction layer when it is released.

	VISIBILITY (Arma 1.8): gadget items (radios) that pass through the pickup/gadget
	lifecycle get their entity hidden by the character gadget system when stowed with
	IsVisibleEquipped()=false. Items displayed in ACM visible slots must stay rendered,
	so every ACM slot attach forces the entity VISIBLE and re-asserts shortly after in
	case the gadget hide lands later in the same frame window. Pouch-default spawned
	radios never enter the gadget lifecycle, which is why only picked-up radios used
	to vanish.

	REPLICATION: visible stored items must remain explicitly replicated or they can disappear
	for remote players. ACM prefabs also keep entity hierarchy updates enabled and inherit
	their replication node from the parent, so pinned visible contents still receive the
	character's movement and teleport updates instead of remaining at the old position.
*/
class ACM_StoredItemLifecycleService
{
	static void SuspendStoredItemPhysics(IEntity entity, out int interactionLayer)
	{
		if (!entity)
			return;

		ACM_ReplicationDiag.Step("5.LIFECYCLE SuspendStoredItemPhysics", entity);

		// Remote observers need visible stored items kept in replication. The prefab's
		// parent-node inheritance keeps this node attached to the character during teleports.
		ACM_SetAlwaysReplicated(entity, true);

		// Keep ACM-slot-stored items rendered even if the gadget system tries to hide
		// them (see header). Re-asserted on delays to beat late gadget-hide callbacks.
		ForceStoredItemVisible(entity);
		if (GetGame() && GetGame().GetCallqueue())
		{
			GetGame().GetCallqueue().CallLater(ForceStoredItemVisible, 100, false, entity);
			GetGame().GetCallqueue().CallLater(ForceStoredItemVisible, 500, false, entity);
		}

		Physics physics = entity.GetPhysics();
		if (!physics)
			return;

		// Do not overwrite the captured layer with 0 when the item is suspended a
		// second time (move between ACM slots) — that would leak "no interaction"
		// into the restore and leave dropped items untargetable.
		int currentLayer = physics.GetInteractionLayer();
		if (currentLayer != 0)
			interactionLayer = currentLayer;

		physics.SetInteractionLayer(0);
	}

	static void RestoreStoredItemPhysics(IEntity entity, int interactionLayer)
	{
		if (!entity)
			return;

		ACM_ReplicationDiag.Step("5.LIFECYCLE RestoreStoredItemPhysics", entity, string.Format("layer=%1", interactionLayer));

		// Once released from an ACM display slot, return the item to normal relevance streaming.
		ACM_SetAlwaysReplicated(entity, false);

		// A zero layer means "nothing was captured" (item had no physics at attach
		// time, or it was already suspended). Restoring 0 would permanently disable
		// interaction, so skip instead.
		if (interactionLayer == 0)
			return;

		Physics physics = entity.GetPhysics();
		if (!physics)
			return;

		physics.SetInteractionLayer(interactionLayer);
	}

	//! Forces the stored item (and its children) to render. Safe to call repeatedly.
	static void ForceStoredItemVisible(IEntity entity)
	{
		if (!entity)
			return;

		bool wasVisible = (entity.GetFlags() & EntityFlags.VISIBLE) != 0;
		entity.SetFlags(EntityFlags.VISIBLE, true);
		if (!wasVisible)
			ACM_ReplicationDiag.Step("5.LIFECYCLE ForceStoredItemVisible flipped VISIBLE on", entity);
	}

	//! Pins visible stored items for remote clients. Streaming can only be changed by authority.
	protected static void ACM_SetAlwaysReplicated(IEntity entity, bool alwaysReplicated)
	{
		if (!entity)
			return;

		RplComponent rpl = RplComponent.Cast(entity.FindComponent(RplComponent));
		if (!rpl || rpl.IsProxy())
			return;

		rpl.EnableStreaming(!alwaysReplicated);
		ACM_ReplicationDiag.Step("7.STREAMING EnableStreaming", entity,
			string.Format("streamingEnabled=%1 (alwaysReplicated=%2)", !alwaysReplicated, alwaysReplicated));
	}
}

