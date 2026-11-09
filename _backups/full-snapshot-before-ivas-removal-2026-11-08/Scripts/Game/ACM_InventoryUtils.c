//------------------------------------------------------------------------------------------------
// ACM_InventoryUtils.c
// Shared utility functions for inventory and character ownership checks
//------------------------------------------------------------------------------------------------

class ACM_InventoryUtils
{
	// Maximum depth for inventory traversal (prevents infinite loops)
	protected static const int MAX_INVENTORY_DEPTH = 15;
	
	//------------------------------------------------------------------------------------------------
	// Check if an inventory item is owned by a specific character
	// Traverses up the inventory hierarchy to find the character owner
	//------------------------------------------------------------------------------------------------
	static bool IsItemOwnedByCharacter(InventoryItemComponent itemComp, ChimeraCharacter character)
	{
		if (!itemComp || !character)
			return false;
		
		int depth = 0;
		InventoryItemComponent current = itemComp;
		
		while (current && depth < MAX_INVENTORY_DEPTH)
		{
			InventoryStorageSlot slot = current.GetParentSlot();
			if (!slot)
				break;
			
			BaseInventoryStorageComponent storage = slot.GetStorage();
			if (!storage)
				break;
			
			IEntity storageOwner = storage.GetOwner();
			if (!storageOwner)
				break;
			
			if (storageOwner == character)
				return true;
			
			current = InventoryItemComponent.Cast(storageOwner.FindComponent(InventoryItemComponent));
			depth++;
		}
		
		return false;
	}
	
	//------------------------------------------------------------------------------------------------
	// Find the character that owns an inventory item
	// Returns the character entity if found, null otherwise
	//------------------------------------------------------------------------------------------------
	static ChimeraCharacter FindCharacterOwner(InventoryItemComponent itemComp)
	{
		if (!itemComp)
			return null;
		
		int depth = 0;
		InventoryItemComponent current = itemComp;
		
		while (current && depth < MAX_INVENTORY_DEPTH)
		{
			InventoryStorageSlot slot = current.GetParentSlot();
			if (!slot)
				break;
			
			BaseInventoryStorageComponent storage = slot.GetStorage();
			if (!storage)
				break;
			
			IEntity storageOwner = storage.GetOwner();
			if (!storageOwner)
				break;
			
			ChimeraCharacter character = ChimeraCharacter.Cast(storageOwner);
			if (character)
				return character;
			
			current = InventoryItemComponent.Cast(storageOwner.FindComponent(InventoryItemComponent));
			depth++;
		}
		
		return null;
	}
	
	//------------------------------------------------------------------------------------------------
	// The character carrying this entity, by ANY attachment route.
	//
	// FindCharacterOwner above follows INVENTORY SLOTS ONLY and gives up the instant a hop is not
	// one - `if (!slot) break` - and again on any intermediate entity carrying no
	// InventoryItemComponent. Both are fine for gear sitting directly in a character's slots, and
	// both fail on a custom mount, where the route is typically
	//
	//     device -> mount adapter (a scene CHILD: no slot, often no inventory component) -> helmet
	//     (a slot) -> character
	//
	// A failed resolve is not harmless here. The remote feed reads the carrier to decide who is
	// OPERATING a camera; no carrier means no operator id, and under the default SQUAD access policy
	// no operator id means the camera is refused outright. So a squadmate's camera reads as missing
	// or disabled rather than as a permissions decision somebody actually made.
	//
	// This tries BOTH hops at every step and stops only when neither is available. It encodes no
	// assumption about how many links there are or what kind each one is, because this mod does not
	// own the mount prefabs.
	//------------------------------------------------------------------------------------------------
	static ChimeraCharacter FindCharacterOwnerAnyRoute(IEntity entity)
	{
		if (!entity)
			return null;

		IEntity current = entity;
		int depth = 0;

		while (current && depth < MAX_INVENTORY_DEPTH)
		{
			ChimeraCharacter character = ChimeraCharacter.Cast(current);
			if (character)
				return character;

			// STORAGE HOP FIRST, and the ordering is not cosmetic.
			//
			// A slotted item also has a scene parent, and that parent is NOT its container - it is
			// wherever the engine attached it. Taking the parent hop first leaps over every
			// intermediate container in one step, which walks past the very chain being inspected.
			InventoryItemComponent itemComp = InventoryItemComponent.Cast(current.FindComponent(InventoryItemComponent));
			if (itemComp)
			{
				InventoryStorageSlot slot = itemComp.GetParentSlot();
				if (slot)
				{
					BaseInventoryStorageComponent storage = slot.GetStorage();
					if (storage && storage.GetOwner())
					{
						current = storage.GetOwner();
						depth++;
						continue;
					}
				}
			}

			// Only when there is no container left to climb.
			IEntity parent = current.GetParent();
			if (!parent)
				return null;

			current = parent;
			depth++;
		}

		return null;
	}

	//------------------------------------------------------------------------------------------------
	// Find character owner from an entity (searches for InventoryItemComponent first)
	//------------------------------------------------------------------------------------------------
	static ChimeraCharacter FindCharacterOwnerFromEntity(IEntity entity)
	{
		if (!entity)
			return null;
		
		InventoryItemComponent itemComp = InventoryItemComponent.Cast(entity.FindComponent(InventoryItemComponent));
		if (!itemComp)
			return null;
		
		return FindCharacterOwner(itemComp);
	}
}
