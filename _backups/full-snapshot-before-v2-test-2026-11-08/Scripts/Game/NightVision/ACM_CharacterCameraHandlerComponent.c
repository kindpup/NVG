//------------------------------------------------------------------------------------------------
// ACM Camera Handler Extension
// Fades ACM head-mounted devices with the wearer's own head, so in first person the wearer looks
// THROUGH the optic rather than at the back of it.
//
// MECHANISM: BaseLoadoutClothComponent.SetAlpha, the same call NVsystem uses.
//
// The alphatest value is handed straight to the device's cloth component, which fades the worn model
// through its own material - the same path the character's head is already using. Three properties
// fall out of that and all three matter:
//
//   The value is PASSED THROUGH, not interpreted. Nothing here decides what counts as first person,
//   so nothing here can get the polarity backwards. The device tracks the head, whatever the engine
//   means by any particular number.
//
//   It is a fade, not a visibility flag. EntityFlags.VISIBLE is a switch with no idea when to turn
//   itself back on, which is why the version built on it needed a hidden-entity list, restore-on-
//   body-change, a menu special case and a poll - and why a device could end up permanently
//   invisible, including in the character and inventory previews, which render the real entity.
//
//   Previews are correct for free. Nothing calls OnAlphatestChange in a preview, so the alpha simply
//   stays where it is and the device renders normally.
//
//------------------------------------------------------------------------------------------------
// FINDING THE DEVICES: SCENE CHILDREN ARE NOT ENOUGH.
//
// GetChildren() does not traverse inventory storage, and a helmet-mounted device lives in the
// HELMET'S STORAGE SLOT rather than as a scene child of anything. So a GetChildren() walk from the
// character never reaches it - which is why the previous versions of this file appeared to work on a
// headstrap and did nothing at all on a helmet rail.
//
// This is the third time this project has hit that trap. The remote feed source component resolved
// its carrier as itself and put the camera at the world origin; the visor authority check silently
// refused to grant control; and now this. Worn and slotted gear is in inventory slots, so any search
// for it has to walk storage as well as children - exactly as ACM_RadialMenuItemsComponent already
// does when it builds the device menu.
//
// Loadout areas are still queried directly as well. They are cheap, they are exact, and they cover
// the case of a device worn in its own area rather than mounted on anything.
//------------------------------------------------------------------------------------------------

modded class SCR_CharacterCameraHandlerComponent
{
	// Depth guard. Storage nests - character, helmet, device - and a malformed hierarchy must not be
	// able to spin here on a per-frame-ish callback.
	protected static const int ACM_MAX_DEPTH = 8;

	// TEMPORARILY ON.
	//
	// Turn this back to false once the alphatest polarity is settled. It is on because the polarity
	// has been guessed at twice and guessed wrong twice, and one line of log ends the argument: the
	// engine's alphatest value is the only thing that decides whether a device should be fading or
	// solid, and nothing in this file should be inferring it.
	protected static const bool ACM_CAM_DEBUG = false;

	//------------------------------------------------------------------------------------------------
	// NVG tube view: hand the final camera transform to the tube camera the moment it is computed.
	// Copying it later from the world camera always got LAST frame's view (measured: ~100 % of moving
	// frames), which made the picture inside the tubes jitter while aiming.
	override void OnAfterCameraUpdate(float pDt, bool pIsKeyframe, inout vector transformMS[4], inout vector transformWS[4])
	{
		super.OnAfterCameraUpdate(pDt, pIsKeyframe, transformMS, transformWS);
		ACM_NVGTubeView.OnPlayerCameraUpdated(GetOwner(), transformWS);
	}

	//------------------------------------------------------------------------------------------------
	override void OnAlphatestChange(int a)
	{
		super.OnAlphatestChange(a);

		// Only ever the local player's own character.
		//
		// Every character in the world carries one of these components. The original resolved its
		// loadout from whatever instance it was running on while resolving hierarchy mounts from
		// GetLocalControlledEntity() - two different characters in one method - so on any instance
		// that was not the local player it applied another player's alphatest to their gear. That was
		// the "other players' NVGs are invisible" report.
		IEntity owner = GetOwner();
		if (!owner || owner != SCR_PlayerController.GetLocalControlledEntity())
			return;

		int applied = 0;

		// Devices worn in a loadout area of their own.
		if (m_LoadoutStorage)
		{
			applied += FadeArea(ACM_Headstrap, a);
			applied += FadeArea(ACM_NV_Slotted, a);
			applied += FadeArea(ACM_NV_ItemSlot, a);
		}

		// Devices mounted on something else - the helmet rail case, and the common one.
		applied += FadeDevices(owner, a, 0);

		if (ACM_CAM_DEBUG)
			Print("[ACM Cam] alphatest=" + a.ToString() + "  devices=" + applied.ToString(), LogLevel.DEBUG);
	}

	//------------------------------------------------------------------------------------------------
	protected int FadeArea(typename area, int alpha)
	{
		return FadeEntity(m_LoadoutStorage.GetClothFromArea(area), alpha);
	}

	//------------------------------------------------------------------------------------------------
	// Walks scene children AND inventory storage, because worn gear is in slots rather than parented.
	//
	// Does not stop at the first match: a character can carry a helmet optic and a spare on a
	// headstrap, and fading only one leaves the other filling the view.
	//------------------------------------------------------------------------------------------------
	protected int FadeDevices(IEntity entity, int alpha, int depth)
	{
		if (!entity || depth > ACM_MAX_DEPTH)
			return 0;

		if (IsHeadMountedDevice(entity))
			return FadeEntity(entity, alpha);

		int applied = 0;

		IEntity child = entity.GetChildren();
		while (child)
		{
			applied += FadeDevices(child, alpha, depth + 1);
			child = child.GetSibling();
		}

		BaseInventoryStorageComponent storage = BaseInventoryStorageComponent.Cast(entity.FindComponent(BaseInventoryStorageComponent));
		if (storage)
		{
			array<IEntity> items = {};
			storage.GetAll(items);

			foreach (IEntity item : items)
			{
				applied += FadeDevices(item, alpha, depth + 1);
			}
		}

		return applied;
	}

	//------------------------------------------------------------------------------------------------
	protected int FadeEntity(IEntity entity, int alpha)
	{
		if (!entity)
			return 0;

		BaseLoadoutClothComponent cloth = BaseLoadoutClothComponent.Cast(entity.FindComponent(BaseLoadoutClothComponent));
		if (!cloth)
			return 0;

		cloth.SetAlpha(alpha);

		return 1;
	}

	//------------------------------------------------------------------------------------------------
	// EITHER component marks a head-mounted device. An IVAS does not have to carry night vision -
	// strip that component off the prefab and it is a pure sensor, and testing only for night vision
	// would leave a sensor-only variant filling the wearer's screen.
	//------------------------------------------------------------------------------------------------
	protected bool IsHeadMountedDevice(IEntity entity)
	{
		if (entity.FindComponent(ACM_NightVisionComponent))
			return true;

		return entity.FindComponent(ACM_TargetDetectionComponent) != null;
	}
}
