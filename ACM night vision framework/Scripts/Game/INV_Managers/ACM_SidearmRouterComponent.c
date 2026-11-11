//
//  ACM
//  ACM Sidearm Router Component
//
//  Original ACM-owned script for the modular framework.
//==================================================================================================

// State machine stages for sidearm routing after Reforger notifies ACM about a weapon change.
enum ACM_ESidearmRouteState
{
	IDLE,
	WAITING_FOR_WEAPON_SWITCH,
	WAITING_FOR_INVENTORY_UNLOCK,
	ROUTING_TO_HOLSTER,
	COOLDOWN,
	FAILED
}

[ComponentEditorProps(category: "GameScripted/Misc", description: "Automatically stows pistols and rifles when switching weapons")]
class ACM_SidearmRouterComponentClass : ScriptComponentClass {}

// Reforger only exposes weapon transitions through animation events, weapon-manager queries, and storage moves.
// ACM turns those hooks into a retrying router that asks policy and route services where the stowed weapon belongs.
class ACM_SidearmRouterComponent : ScriptComponent
{
	protected SCR_CharacterControllerComponent m_CharacterController;
	protected BaseWeaponManagerComponent m_WeaponManager;
	protected SCR_InventoryStorageManagerComponent m_InventoryManager;

	protected IEntity m_LastObservedWeapon;
	protected IEntity m_LastKnownSidearm;
	protected IEntity m_LastKnownRifle;
	protected bool m_bRouteBusy = false;

	protected ACM_ESidearmRouteState m_eRouteState = ACM_ESidearmRouteState.IDLE;
	protected IEntity m_QueuedWeapon;
	protected int m_iRouteRetryCount = 0;

	[Attribute(defvalue: "25", desc: "Retry delay in ms for sidearm holster routing")]
	protected int m_iRouteRetryDelayMs;

	[Attribute(defvalue: "120", desc: "Retry limit for sidearm holster routing")]
	protected int m_iRouteRetryLimit;

	protected AnimationEventID m_iDrawSidearmEvent;
	protected AnimationEventID m_iStowSidearmEvent;

	// Registers the animation event ids once so runtime event handling can compare ids instead of strings.
	private void ACM_SidearmRouterComponent(IEntityComponentSource src, IEntity ent, IEntity parent)
	{
		m_iDrawSidearmEvent = GameAnimationUtils.RegisterAnimationEvent("Weapon_Change_Show");
		m_iStowSidearmEvent = GameAnimationUtils.RegisterAnimationEvent("Weapon_Change_Hide");
	}

	// Requests the INIT event because Reforger script components only receive EOnInit when the mask includes it.
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		SetEventMask(owner, EntityEvent.INIT);
	}

	// Resolves engine components, subscribes to animation hooks, and starts a polling fallback for weapon-state timing gaps.
	override void EOnInit(IEntity owner)
	{
		super.EOnInit(owner);

		m_CharacterController = SCR_CharacterControllerComponent.Cast(owner.FindComponent(SCR_CharacterControllerComponent));
		m_InventoryManager = SCR_InventoryStorageManagerComponent.Cast(owner.FindComponent(SCR_InventoryStorageManagerComponent));
		m_WeaponManager = BaseWeaponManagerComponent.Cast(owner.FindComponent(BaseWeaponManagerComponent));

		if (m_CharacterController)
			m_CharacterController.GetOnAnimationEvent().Insert(OnAnimationEvent);

		ObserveCurrentWeapon();
		GetGame().GetCallqueue().CallLater(ObserveCurrentWeapon, 100, true);
	}

	// Cleans up engine subscriptions and any queued retries when the component is destroyed.
	override void OnDelete(IEntity owner)
	{
		if (m_CharacterController)
			m_CharacterController.GetOnAnimationEvent().Remove(OnAnimationEvent);

		GetGame().GetCallqueue().Remove(ObserveCurrentWeapon);
		GetGame().GetCallqueue().Remove(AdvanceSidearmRoute);

		super.OnDelete(owner);
	}

	// Polling fallback that remembers the last observed weapon and the last valid stowed weapons between animation events.
	// Early-exits when the weapon hasn't changed since the last poll to avoid redundant
	// classification work on every 100 ms tick per player.
	protected void ObserveCurrentWeapon()
	{
		if (!m_WeaponManager)
			return;

		BaseWeaponComponent current = m_WeaponManager.GetCurrentWeapon();
		if (!current)
			return;

		IEntity currentOwner = current.GetOwner();
		if (!currentOwner)
			return;

		// No classification work needed when the weapon in hand hasn't changed.
		if (currentOwner == m_LastObservedWeapon)
			return;

		if (ACM_ItemClassificationService.IsHolsterSidearm(currentOwner))
			m_LastKnownSidearm = currentOwner;
		else if (ACM_ItemClassificationService.IsRifle(currentOwner))
			m_LastKnownRifle = currentOwner;

		m_LastObservedWeapon = currentOwner;
	}

	// Converts raw Reforger animation events into ACM transition snapshots and forwards them into the routing state machine.
	protected void OnAnimationEvent(AnimationEventID animEventType, AnimationEventID animUserString, int intParam, float timeFromStart, float timeToEnd, SCR_CharacterControllerComponent ctrl)
	{
		if (animEventType == m_iDrawSidearmEvent || animEventType == m_iStowSidearmEvent)
		{
			ACM_SidearmTransitionContext transition = BuildTransitionContext(animEventType);
			ApplyTransitionContext(transition);
			m_LastObservedWeapon = transition.CurrentWeapon;
		}
	}

	// Snapshots the current live weapon state together with the previously observed weapon for one routing decision.
	protected ACM_SidearmTransitionContext BuildTransitionContext(AnimationEventID transitionEvent)
	{
		BaseWeaponComponent current = null;
		if (m_WeaponManager)
			current = m_WeaponManager.GetCurrentWeapon();

		IEntity currentEntity = null;
		EWeaponType currentType = EWeaponType.WT_NONE;

		if (current)
		{
			currentEntity = current.GetOwner();
			currentType = current.GetWeaponType();
		}

		return new ACM_SidearmTransitionContext(transitionEvent, m_LastObservedWeapon, currentEntity, currentType);
	}

	// True unless this machine is merely observing the character. Animation events
	// fire on EVERY machine (server, owning client, remote proxies); a remote proxy
	// must never issue storage moves for another player's weapon or its local
	// inventory graph diverges from the authority.
	protected bool ACM_ShouldDriveRouting()
	{
		return ACM_ReplicationAuthority.IsLocalDriverFor(GetOwner());
	}

	// Handles one transition by either returning a drawn routed weapon to hands or queueing a stowed routed weapon for storage.
	protected void ApplyTransitionContext(ACM_SidearmTransitionContext transition)
	{
		if (!transition)
			return;

		if (!ACM_ShouldDriveRouting())
			return;

		if (transition.TransitionEvent == m_iDrawSidearmEvent)
		{
			if (ShouldReturnStoredWeaponToHands(transition))
				ACM_ReturnStoredWeaponToHands(transition.CurrentWeapon);

			return;
		}

		IEntity weaponToStore = SelectWeaponForStorage(transition);
		if (!weaponToStore)
			return;

		QueueWeaponStorage(weaponToStore);
	}

	// Prevents hotbar bookkeeping and duplicate draw events from yanking a routed weapon out unless it is actually stored.
	protected bool ShouldReturnStoredWeaponToHands(ACM_SidearmTransitionContext transition)
	{
		if (!transition || !transition.CurrentWeapon || !m_InventoryManager)
			return false;

		if (!ACM_IsHolsterRoutableWeapon(transition.CurrentWeapon, transition.CurrentIsSidearm, transition.CurrentIsRifle))
			return false;

		if (transition.PreviousWeapon == transition.CurrentWeapon)
			return false;

		return ACM_SidearmHolsterLocator.IsStoredInHolster(m_InventoryManager, transition.CurrentWeapon);
	}

	// Chooses which routed weapon should be stored, preferring the transition data and falling back to the last polled weapon when timing is late.
	protected IEntity SelectWeaponForStorage(ACM_SidearmTransitionContext transition)
	{
		if (!transition || transition.TransitionEvent != m_iStowSidearmEvent)
			return null;

		if (transition.CurrentWeapon && ACM_IsHolsterRoutableWeapon(transition.CurrentWeapon, transition.CurrentIsSidearm, transition.CurrentIsRifle))
			return transition.CurrentWeapon;

		if (transition.PreviousWeapon && transition.PreviousWasSidearm && !transition.CurrentIsSidearm)
			return transition.PreviousWeapon;

		if (transition.PreviousWeapon && ACM_IsHolsterRoutableWeapon(transition.PreviousWeapon, transition.PreviousWasSidearm, transition.PreviousWasRifle)
			&& !ACM_IsHolsterRoutableWeapon(transition.CurrentWeapon, transition.CurrentIsSidearm, transition.CurrentIsRifle))
			return transition.PreviousWeapon;

		if (m_LastKnownSidearm && !ACM_IsHolsterRoutableWeapon(transition.CurrentWeapon, transition.CurrentIsSidearm, transition.CurrentIsRifle))
			return m_LastKnownSidearm;

		if (m_LastKnownRifle && !ACM_IsHolsterRoutableWeapon(transition.CurrentWeapon, transition.CurrentIsSidearm, transition.CurrentIsRifle))
			return m_LastKnownRifle;

		return null;
	}

	// Falls back to live route-candidate scoring so compatible long-gun variants still route even if weapon enums differ across mods.
	protected bool ACM_IsHolsterRoutableWeapon(IEntity weapon, bool isKnownSidearm = false, bool isKnownRifle = false)
	{
		if (!weapon)
			return false;

		if (isKnownSidearm || ACM_ItemClassificationService.IsHolsterSidearm(weapon))
			return true;

		if (isKnownRifle)
			return true;

		if (!m_InventoryManager)
			return false;

		ACM_StorageRouteCandidate routeCandidate = ACM_StorageRouteResolver.ResolveBestCandidateForMove(m_InventoryManager, weapon, true);
		return routeCandidate && routeCandidate.Score > 0;
	}

	// Starts routing only for supported weapons that are not already stored and avoids queuing duplicate work.
	protected void QueueWeaponStorage(IEntity weapon)
	{
		if (!weapon || !m_InventoryManager)
			return;

		if (m_eRouteState != ACM_ESidearmRouteState.IDLE && m_QueuedWeapon == weapon)
			return;

		if (ACM_SidearmHolsterLocator.IsStoredInHolster(m_InventoryManager, weapon))
			return;

		BeginQueuedStorage(weapon);
	}

	// Resets retry counters and enters the first state of the routing state machine.
	protected void BeginQueuedStorage(IEntity weapon)
	{
		m_QueuedWeapon = weapon;
		m_iRouteRetryCount = 0;
		m_eRouteState = ACM_ESidearmRouteState.WAITING_FOR_WEAPON_SWITCH;

		ScheduleRouteAdvance(0);
	}

	// Core routing state machine: wait for the switch to finish, wait for inventory access, then try the holster move with retries.
	protected void AdvanceSidearmRoute()
	{
		// Re-checked each tick: ownership/authority can change mid-route (possession
		// transfer, player disconnect). A machine that stopped being the driver must
		// abandon the route instead of issuing storage moves it no longer owns.
		if (!ACM_ShouldDriveRouting())
		{
			CompleteQueuedStorage(false);
			return;
		}

		if (m_eRouteState == ACM_ESidearmRouteState.COOLDOWN)
		{
			CompleteQueuedStorage(true);
			return;
		}

		if (m_eRouteState == ACM_ESidearmRouteState.FAILED)
		{
			CompleteQueuedStorage(false);
			return;
		}

		if (!m_QueuedWeapon || !m_InventoryManager)
			return;

		if (ACM_SidearmHolsterLocator.IsStoredInHolster(m_InventoryManager, m_QueuedWeapon))
		{
			CompleteQueuedStorage(true);
			return;
		}

		switch (m_eRouteState)
		{
			case ACM_ESidearmRouteState.WAITING_FOR_WEAPON_SWITCH:
				if (IsQueuedWeaponCurrentWeapon())
				{
					RetryQueuedStorage(ACM_ESidearmRouteState.WAITING_FOR_WEAPON_SWITCH);
					return;
				}

				if (!IsWeaponSwitchComplete())
				{
					RetryQueuedStorage(ACM_ESidearmRouteState.WAITING_FOR_WEAPON_SWITCH);
					return;
				}

				m_eRouteState = ACM_ESidearmRouteState.WAITING_FOR_INVENTORY_UNLOCK;
				AdvanceSidearmRoute();
				return;

			case ACM_ESidearmRouteState.WAITING_FOR_INVENTORY_UNLOCK:
				if (!CanRouteNow())
				{
					RetryQueuedStorage(ACM_ESidearmRouteState.WAITING_FOR_INVENTORY_UNLOCK);
					return;
				}

				m_eRouteState = ACM_ESidearmRouteState.ROUTING_TO_HOLSTER;
				AdvanceSidearmRoute();
				return;

			case ACM_ESidearmRouteState.ROUTING_TO_HOLSTER:
				if (TryStoreQueuedWeapon())
				{
					m_eRouteState = ACM_ESidearmRouteState.COOLDOWN;
					ScheduleRouteAdvance(m_iRouteRetryDelayMs);
					return;
				}

				RetryQueuedStorage(ACM_ESidearmRouteState.WAITING_FOR_INVENTORY_UNLOCK);
				return;
		}
	}

	// Combines Reforger readiness signals so ACM only moves the sidearm after the engine finishes the weapon transition.
	protected bool IsWeaponSwitchComplete()
	{
		if (!m_InventoryManager)
			return false;

		if (!m_InventoryManager.IsAnimationReady())
			return false;

		if (m_CharacterController && m_CharacterController.IsChangingItem())
			return false;

		return !IsQueuedWeaponCurrentWeapon();
	}

	// Checks whether the engine still considers the queued weapon to be the active weapon.
	protected bool IsQueuedWeaponCurrentWeapon()
	{
		if (!m_WeaponManager || !m_QueuedWeapon)
			return false;

		BaseWeaponComponent current = m_WeaponManager.GetCurrentWeapon();
		if (!current)
			return false;

		return current.GetOwner() == m_QueuedWeapon;
	}

	// Final lightweight gate before issuing a move request into inventory storage.
	protected bool CanRouteNow()
	{
		if (!m_InventoryManager)
			return false;

		if (!m_InventoryManager.IsAnimationReady())
			return false;

		return !m_bRouteBusy;
	}

	// Serializes one storage move attempt so overlapping retries cannot issue duplicate storage moves.
	protected bool TryStoreQueuedWeapon()
	{
		if (!m_QueuedWeapon || m_bRouteBusy)
			return false;

		m_bRouteBusy = true;

		bool stored = TryStoreWeaponInHolster(m_QueuedWeapon);
		m_bRouteBusy = false;

		return stored;
	}

	// Advances the retry counter and either schedules the next attempt or marks the route as failed.
	protected void RetryQueuedStorage(ACM_ESidearmRouteState nextState)
	{
		if (m_iRouteRetryCount >= m_iRouteRetryLimit)
		{
			m_eRouteState = ACM_ESidearmRouteState.FAILED;
			ScheduleRouteAdvance(0);
			return;
		}

		m_iRouteRetryCount++;
		m_eRouteState = nextState;

		ScheduleRouteAdvance(m_iRouteRetryDelayMs);
	}

	// Keeps only one pending retry alive so the router state machine stays deterministic.
	protected void ScheduleRouteAdvance(int delayMs)
	{
		GetGame().GetCallqueue().Remove(AdvanceSidearmRoute);
		GetGame().GetCallqueue().CallLater(AdvanceSidearmRoute, delayMs, false);
	}

	// Resets transient routing state after success or failure.
	protected void CompleteQueuedStorage(bool success)
	{
		GetGame().GetCallqueue().Remove(AdvanceSidearmRoute);

		m_bRouteBusy = false;
		m_eRouteState = ACM_ESidearmRouteState.IDLE;
		m_QueuedWeapon = null;
		m_iRouteRetryCount = 0;
	}

	// Finds the best holster-style candidate through the shared route resolver and performs the actual move.
	protected bool TryStoreWeaponInHolster(IEntity weapon)
	{
		if (!weapon || !m_InventoryManager)
			return false;

		if (ACM_SidearmHolsterLocator.IsStoredInHolster(m_InventoryManager, weapon))
			return true;

		ACM_StorageRouteCandidate routeCandidate = ACM_StorageRouteResolver.ResolveBestCandidateForMove(m_InventoryManager, weapon, true);
		if (!routeCandidate || routeCandidate.Score <= 0)
			return ACM_SidearmHolsterLocator.IsStoredInHolster(m_InventoryManager, weapon);

		bool moved = ACM_StorageRouteResolver.TryRouteToCandidate(m_InventoryManager, weapon, routeCandidate, true, null);

		if (moved)
			return true;

		return ACM_SidearmHolsterLocator.IsStoredInHolster(m_InventoryManager, weapon);
	}

	// Returns a stored weapon to the player's hands when Reforger switches back to it.
	protected void ACM_ReturnStoredWeaponToHands(IEntity weapon)
	{
		if (!weapon || !m_InventoryManager)
			return;

		SCR_HandSlotStorageComponent handStorage = GetHandStorage();
		if (!handStorage)
			return;

		m_InventoryManager.TryMoveItemToStorage(weapon, handStorage);
	}

	// Locates the owner's hand-slot storage used for active weapon placement.
	protected SCR_HandSlotStorageComponent GetHandStorage()
	{
		return SCR_HandSlotStorageComponent.Cast(GetOwner().FindComponent(SCR_HandSlotStorageComponent));
	}
}
