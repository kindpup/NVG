//
//  ACM
//  ACM Replication Diagnostics
//
//  Step-by-step logging of the whole pouch chain:
//     Character -> Belt/Vest -> Pouch (ACM storage) -> Mag/Radio -> Server -> other players
//
//  Every line carries:  machine=SERVER|CLIENT   item role (item's OWN RplComponent)
//                       root role (character's RplComponent)   so logs from the three
//  machines (server / owner client / observing client) can be lined up side by side.
//
//  >>> SET ENABLED TO false (OR DELETE THIS FILE) BEFORE SHIPPING <<<
//==================================================================================================

class ACM_ReplicationDiag
{
	// Keep OFF in shipping builds: LogAttach schedules deferred checks holding raw
	// InventoryStorageSlot refs for up to 20s, which can outlive streamed-out proxy gear on clients.
	static const bool ENABLED = false;

	//------------------------------------------------------------------------------------------------
	// Generic step logger. Use from anywhere in the chain.
	static void Step(string step, IEntity item, string extra = "")
	{
		if (!ENABLED)
			return;

		Print(string.Format("[ACM-STEP] %1 | %2 | item=%3 %4 | %5",
			step, Machine(), PrefabOf(item), RoleOf(item), extra), LogLevel.NORMAL);
	}

	// Same as Step, but also dumps the storage/slot the item is targeting.
	static void StepStorage(string step, IEntity item, BaseInventoryStorageComponent storage, int slotID, string extra = "")
	{
		if (!ENABLED)
			return;

		Step(step, item, string.Format("storage=%1 slot=%2 %3", DescribeStorage(storage), slotID, extra));
	}

	//------------------------------------------------------------------------------------------------
	// Slot lifecycle. Called from every ACM slot OnAttachedEntity / OnDetachedEntity.
	static void LogAttach(InventoryStorageSlot slot, IEntity item)
	{
		if (!ENABLED || !slot || !item)
			return;

		Print(string.Format("[ACM-DIAG] ATTACH %1 | %2", Machine(), Describe(slot, item)), LogLevel.NORMAL);

		// Re-check over time: the observer reports "visible for a split second, then gone".
		// These fire on every machine so we can see WHEN and WHERE the entity goes away.
		if (GetGame() && GetGame().GetCallqueue())
		{
			GetGame().GetCallqueue().CallLater(LogDelayedCheck, 1000, false, slot, item, 1);
			GetGame().GetCallqueue().CallLater(LogDelayedCheck, 3000, false, slot, item, 3);
			GetGame().GetCallqueue().CallLater(LogDelayedCheck, 6000, false, slot, item, 6);
			GetGame().GetCallqueue().CallLater(LogDelayedCheck, 10000, false, slot, item, 10);
			GetGame().GetCallqueue().CallLater(LogDelayedCheck, 20000, false, slot, item, 20);
		}
	}

	static void LogDetach(InventoryStorageSlot slot, IEntity item)
	{
		if (!ENABLED || !slot)
			return;

		Print(string.Format("[ACM-DIAG] DETACH %1 | %2", Machine(), Describe(slot, item)), LogLevel.NORMAL);
	}

	protected static void LogDelayedCheck(InventoryStorageSlot slot, IEntity item, int seconds)
	{
		if (!ENABLED)
			return;

		// A deleted entity arrives here as null: that is the "entity destroyed / streamed out" case.
		if (!item)
		{
			Print(string.Format("[ACM-DIAG] CHECK+%1s %2 | ENTITY GONE (deleted or streamed out on this machine) slot=%3",
				seconds, Machine(), SlotName(slot)), LogLevel.WARNING);
			return;
		}

		bool stillAttached = slot && slot.GetAttachedEntity() == item;
		Print(string.Format("[ACM-DIAG] CHECK+%1s %2 | stillAttached=%3 %4",
			seconds, Machine(), stillAttached, Describe(slot, item)), LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	// Callback wrapper so we see whether the SERVER accepted or rejected a Try* request.
	static SCR_InvCallBack WrapCallback(SCR_InvCallBack cb, string step, IEntity item)
	{
		// Only substitute when the caller passed no callback; an existing callback
		// (UI / BLE) must keep receiving the result untouched.
		if (!ENABLED || cb)
			return cb;

		ACM_DiagInvCallback diag = new ACM_DiagInvCallback();
		diag.m_sACM_Step = step;
		diag.m_ACM_Item = item;
		return diag;
	}

	//------------------------------------------------------------------------------------------------
	// Helpers
	static string Machine()
	{
		if (!Replication.IsRunning())
			return "machine=OFFLINE";

		if (Replication.IsServer())
			return "machine=SERVER";

		return "machine=CLIENT";
	}

	//! Role of the entity's OWN RplComponent plus the role of its root (character).
	static string RoleOf(IEntity entity)
	{
		if (!entity)
			return "itemRpl=<null-entity>";

		RplComponent ownRpl = RplComponent.Cast(entity.FindComponent(RplComponent));
		string own = ACM_RplRoleText(ownRpl);

		IEntity root = entity.GetRootParent();
		string rootRole = "";
		if (root && root != entity)
		{
			RplComponent rootRpl = RplComponent.Cast(root.FindComponent(RplComponent));
			string rootRoleText = ACM_RplRoleText(rootRpl);
			rootRole = string.Format(" root=%1 rootRpl=%2", PrefabOf(root), rootRoleText);
		}

		return string.Format("itemRpl=%1%2", own, rootRole);
	}

	protected static string ACM_RplRoleText(RplComponent rpl)
	{
		if (!rpl)
			return "NORPL";

		string s = "";
		if (rpl.IsMaster())
			s = "MASTER";
		else
			s = "PROXY";

		if (rpl.IsOwner())
			s = s + "+OWNER";
		if (rpl.IsRemoteProxy())
			s = s + "+REMOTE";

		RplId id = rpl.Id();
		return string.Format("%1(id=%2)", s, id);
	}

	static string DescribeStorage(BaseInventoryStorageComponent storage)
	{
		if (!storage)
			return "<null-storage>";

		return string.Format("%1@%2", storage.ClassName(), PrefabOf(storage.GetOwner()));
	}

	protected static string SlotName(InventoryStorageSlot slot)
	{
		if (!slot)
			return "<null-slot>";

		return slot.GetSourceName();
	}

	protected static string Describe(InventoryStorageSlot slot, IEntity item)
	{
		string slotName = SlotName(slot);
		string storageDesc = "<no-storage>";
		string occluded = "?";
		vector rootPos = vector.Zero;
		IEntity root = null;

		if (slot)
		{
			BaseInventoryStorageComponent storage = slot.GetStorage();
			storageDesc = DescribeStorage(storage);

			EquipmentStorageSlot eqSlot = EquipmentStorageSlot.Cast(slot);
			if (eqSlot)
				occluded = eqSlot.IsOccluded().ToString();

			if (storage && storage.GetOwner())
			{
				root = storage.GetOwner().GetRootParent();
				if (root)
					rootPos = root.GetOrigin();
			}
		}

		string itemInfo = "item=<null-item>";
		if (item)
		{
			bool hasMesh = item.GetVObject() != null;
			bool visibleFlag = (item.GetFlags() & EntityFlags.VISIBLE) != 0;
			vector delta = item.GetOrigin() - rootPos;

			string parentName = "<no-parent>";
			IEntity parent = item.GetParent();
			if (parent)
				parentName = PrefabOf(parent);

			string layer = "nophys";
			Physics ph = item.GetPhysics();
			if (ph)
				layer = ph.GetInteractionLayer().ToString();

			itemInfo = string.Format("item=%1 %2 mesh=%3 visibleFlag=%4 physLayer=%5 deltaFromRoot=%6 parent=%7",
				PrefabOf(item), RoleOf(item), hasMesh, visibleFlag, layer, delta.ToString(), parentName);
		}

		return string.Format("storage=%1 slot=%2 occluded=%3 %4", storageDesc, slotName, occluded, itemInfo);
	}

	static string PrefabOf(IEntity entity)
	{
		if (!entity)
			return "<null>";

		EntityPrefabData prefabData = entity.GetPrefabData();
		if (!prefabData)
			return entity.ToString();

		string prefabName = prefabData.GetPrefabName();
		if (prefabName.IsEmpty())
			return entity.ToString();

		int lastSlash = -1;
		for (int i = prefabName.Length() - 1; i >= 0; i--)
		{
			if (prefabName.Get(i) == "/")
			{
				lastSlash = i;
				break;
			}
		}

		if (lastSlash >= 0)
			return prefabName.Substring(lastSlash + 1, prefabName.Length() - lastSlash - 1);

		return prefabName;
	}
}

//! Logs the server's answer to a TryInsertItemInStorage / TryMoveItemToStorage request.
class ACM_DiagInvCallback : SCR_InvCallBack
{
	string m_sACM_Step;
	IEntity m_ACM_Item;

	override protected void OnComplete()
	{
		ACM_ReplicationDiag.Step(m_sACM_Step + ":SERVER-ACCEPTED", m_ACM_Item);
	}

	override protected void OnFailed()
	{
		ACM_ReplicationDiag.Step(m_sACM_Step + ":SERVER-REJECTED", m_ACM_Item);
	}
}

