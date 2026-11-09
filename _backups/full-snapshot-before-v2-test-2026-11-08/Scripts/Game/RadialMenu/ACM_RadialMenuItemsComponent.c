//------------------------------------------------------------------------------------------------
// ACM_RadialMenuItemsComponent.c
// Radial menu component for ACM weapon attachments AND helmet/body equipment
// Part of ACM Night Vision and Lights mod
//------------------------------------------------------------------------------------------------

// Constants for imageset
const string ACM_RADIAL_IMAGESET = "{C258361975CC6C9D}UI/Textures/Icons/ACM_RadialMenu_Icons.imageset";

//------------------------------------------------------------------------------------------------
// IVAS visor flip.
//
// Device-scoped rather than player-scoped: it sits under the device's own category alongside its
// NVG and thermal toggles, because it acts on that specific piece of hardware. Only offered for a
// device whose ACM_TargetDetectionComponent has the IVAS visor option enabled - a conventional NVG
// has no independent visor, so showing the entry would be offering something that does nothing.
//------------------------------------------------------------------------------------------------
class ACM_IvasVisorEntry : SCR_SelectionMenuEntry
{
	protected IEntity m_Owner;
	protected bool m_bPerformed;

	void ACM_IvasVisorEntry(IEntity owner)
	{
		m_Owner = owner;
		SetIcon(ACM_RADIAL_IMAGESET, "nvg");
		Update();
	}

	protected ACM_TargetDetectionComponent ResolveDetection()
	{
		if (!m_Owner)
			return null;

		return ACM_TargetDetectionComponent.Cast(m_Owner.FindComponent(ACM_TargetDetectionComponent));
	}

	override void Update()
	{
		ACM_TargetDetectionComponent detection = ResolveDetection();

		if (!detection)
		{
			SetName("Visor");
			return;
		}

		// Authority missing is its own state, not a hidden entry.
		//
		// This used to be gated out of existence: the menu only offered the visor when this machine
		// had authority, so any moment authority had not resolved - the tick after a teleport, the
		// tick after a possession - the entry was gone and there was no way at all to switch the
		// device on. Saying NO CONTROL is usable and it tells the player something true.
		if (!detection.HasLocalAuthority())
		{
			SetName("Visor [NO CONTROL]");
			return;
		}

		// States the CURRENT position rather than the action it will perform, matching how the
		// mod's other stateful entries read.
		if (detection.IsVisorDown())
			SetName("Visor [DOWN]");
		else
			SetName("Visor [UP]");
	}

	override void OnPerform()
	{
		if (m_bPerformed)
			return;

		m_bPerformed = true;

		ACM_TargetDetectionComponent detection = ResolveDetection();
		if (detection)
			detection.ToggleVisor();
	}
}

//------------------------------------------------------------------------------------------------
// IVAS SETTINGS -> TACTICAL ROLE
//
// The radial menu is only a FRONTEND for the role API. It owns no role state, caches nothing and
// makes no decisions - it calls ACM_TacticalRoleComponent.RequestSetTacticalRole and stops. A
// mission framework, a loadout system, a squad leader assigning roles or an admin override all
// call the same method, and none of them route through here.
//
// Structurally this is the same three-level shape the mod already uses for PEQ-15 -> IR Laser
// Colour -> Red, so it needs no new menu machinery. Roles are split across two sub-categories
// because fifteen wedges on one ring is unreadable - the existing submenus all sit at eight or
// fewer, and that is the geometry this menu was built for.
//------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------
// One role. Selecting it applies immediately - no confirm step, matching every other setting in
// this menu - and the entry marks itself so the current role is visible without leaving the ring.
//------------------------------------------------------------------------------------------------
class ACM_TacticalRoleEntry : SCR_SelectionMenuEntry
{
	protected ACM_ETacticalRole m_eRole;
	protected string m_sDisplayName;
	protected bool m_bPerformed;

	void ACM_TacticalRoleEntry(ACM_ETacticalRole role, string displayName)
	{
		m_eRole = role;
		m_sDisplayName = displayName;

		// One shared icon: the imageset has no per-role art yet, and inventing fifteen names that
		// resolve to nothing would leave every wedge blank rather than merely identical.
		SetIcon(ACM_RADIAL_IMAGESET, "category");
		SetName(m_sDisplayName);
	}

	override void Update()
	{
		ACM_TacticalRoleComponent registry = ACM_TacticalRoleComponent.GetInstance();

		if (registry && registry.GetLocalTacticalRole() == m_eRole)
			SetName(string.Format("%1 [ACTIVE]", m_sDisplayName));
		else
			SetName(m_sDisplayName);
	}

	override void OnPerform()
	{
		// Same double-fire guard the mod's other dynamic entries use.
		if (m_bPerformed)
			return;

		m_bPerformed = true;

		ACM_TacticalRoleComponent registry = ACM_TacticalRoleComponent.GetInstance();
		if (!registry)
			return;

		registry.RequestSetTacticalRole(m_eRole);

		// Feedback, because the menu closes on perform and the marker change happens out on other
		// people's displays rather than on the selecting player's own.
		SCR_HintManagerComponent.ShowCustomHint(string.Format("Tactical Role: %1", m_sDisplayName), "ACM IVAS", 3);
	}
}

//------------------------------------------------------------------------------------------------
// A group of roles.
//------------------------------------------------------------------------------------------------
class ACM_TacticalRoleGroupCategory : SCR_SelectionMenuCategoryEntry
{
	void ACM_TacticalRoleGroupCategory(string name)
	{
		SetIcon(ACM_RADIAL_IMAGESET, "category");
		SetName(name);
	}
}

//------------------------------------------------------------------------------------------------
// The Tactical Role root. Its label carries the current role, so a player can read what they are
// set to without opening it - the same pattern the light colour categories use.
//------------------------------------------------------------------------------------------------
class ACM_TacticalRoleCategory : SCR_SelectionMenuCategoryEntry
{
	void ACM_TacticalRoleCategory()
	{
		SetIcon(ACM_RADIAL_IMAGESET, "category");
		Update();
	}

	override void Update()
	{
		ACM_TacticalRoleComponent registry = ACM_TacticalRoleComponent.GetInstance();
		if (!registry)
		{
			SetName("Tactical Role");
			return;
		}

		ACM_ETacticalRole declared = registry.GetLocalTacticalRole();

		// Declared and effective are shown differently on purpose. A player who has chosen sees
		// their choice; one who has not sees what the fallback is currently deriving for them,
		// marked as automatic so it is never mistaken for a selection they made.
		if (declared != ACM_ETacticalRole.UNASSIGNED)
		{
			SetName(string.Format("Tactical Role [%1]", ACM_TacticalRoleCatalog.GetPreset(declared).m_sDisplayName));
			return;
		}

		IEntity local = SCR_PlayerController.GetLocalControlledEntity();
		PlayerController controller = GetGame().GetPlayerController();
		if (!local || !controller)
		{
			SetName("Tactical Role");
			return;
		}

		ACM_ETacticalRole effective = registry.GetEffectiveTacticalRole(controller.GetPlayerId(), local);
		SetName(string.Format("Tactical Role [Auto: %1]", ACM_TacticalRoleCatalog.GetPreset(effective).m_sDisplayName));
	}
}

[EntityEditorProps(category: "GameScripted/UI/RadialMenu", description: "ACM Attachment Radial Menu")]
class ACM_RadialMenuItemsComponentClass : SCR_RadialMenuItemsComponentClass {}

class ACM_RadialMenuItemsComponent : SCR_RadialMenuItemsComponent
{
	protected ref set<IEntity> m_AddedEntities = new set<IEntity>();
	
	protected bool HasACMComponent(IEntity entity)
	{
		if (!entity)
			return false;
		
		if (ACM_NVL_WeaponLightComponent.Cast(entity.FindComponent(ACM_NVL_WeaponLightComponent)))
			return true;
		
		if (ACM_NVL_WeaponLaserComponent.Cast(entity.FindComponent(ACM_NVL_WeaponLaserComponent)))
			return true;
		
		if (ACM_NVL_LightsComponent.Cast(entity.FindComponent(ACM_NVL_LightsComponent)))
			return true;
		
		if (ACM_NVL_StrobeComponent.Cast(entity.FindComponent(ACM_NVL_StrobeComponent)))
			return true;
		
		if (ACM_NightVisionComponent.Cast(entity.FindComponent(ACM_NightVisionComponent)))
			return true;
		
		if (ACM_ThermalComponentV2.Cast(entity.FindComponent(ACM_ThermalComponentV2)))
			return true;
		
		if (ACM_ThermalTestComponent.Cast(entity.FindComponent(ACM_ThermalTestComponent)))
			return true;
		
		return false;
	}
	
	protected string GetDeviceName(IEntity entity)
	{
		InventoryItemComponent itemComp = InventoryItemComponent.Cast(
			entity.FindComponent(InventoryItemComponent));
		
		if (itemComp)
		{
			UIInfo info = itemComp.GetUIInfo();
			if (info)
			{
				string name = WidgetManager.Translate(info.GetName());
				if (!name.IsEmpty())
					return name;
			}
		}
		
		return "Device";
	}
	
	protected void AddEntityToMenu(notnull SCR_RadialMenu radialMenu, notnull IEntity entity, string prefix = "")
	{
		if (m_AddedEntities.Contains(entity))
			return;
		
		m_AddedEntities.Insert(entity);
		
		string deviceName = GetDeviceName(entity);
		
		
		ACM_SelectionMenuCategory category = new ACM_SelectionMenuCategory(entity, deviceName);

		array<Managed> lightComponentsManaged = {};
		entity.FindComponents(ACM_NVL_LightsComponent, lightComponentsManaged);

		bool hasAnyLightsComp = false;
		bool hasFlashlightType = false;
		bool hasIRType = false;
		bool hasWeaponLaserFlashlight = false;
		bool hasWeaponLaserIR = false;
		
		foreach (Managed lightManaged : lightComponentsManaged)
		{
			ACM_NVL_LightsComponent foundComp = ACM_NVL_LightsComponent.Cast(lightManaged);
			if (!foundComp)
				continue;

			hasAnyLightsComp = true;
			if (foundComp.HasLightType(ACM_NVL_LightType.FLASHLIGHT))
			{
				hasFlashlightType = true;
				if (ACM_NVL_WeaponLaserComponent.Cast(foundComp))
					hasWeaponLaserFlashlight = true;
			}
			if (foundComp.HasLightType(ACM_NVL_LightType.IRLIGHT))
			{
				hasIRType = true;
				if (ACM_NVL_WeaponLaserComponent.Cast(foundComp))
					hasWeaponLaserIR = true;
			}
		}

		ACM_NVL_LightsComponent lightsComp = null;
		if (hasAnyLightsComp)
			lightsComp = ACM_NVL_LightsComponent.Cast(entity.FindComponent(ACM_NVL_LightsComponent));
		
		ACM_NVL_StrobeComponent strobeComp = ACM_NVL_StrobeComponent.Cast(
			entity.FindComponent(ACM_NVL_StrobeComponent));
		
		if (lightsComp && hasAnyLightsComp)
		{
			// Pattern: Device → Cover → Color
			
			// FLASHLIGHT: Toggle → Cover → Color
			if (hasFlashlightType)
			{
				// 1. Device toggle
				ACM_DynamicMenuEntry flashEntry = new ACM_DynamicMenuEntry(entity, ACM_DynamicActionType.TOGGLE_FLASHLIGHT, deviceName);
				category.AddEntry(flashEntry);
				
				// 2. Cover toggle (only if device has IR capability too)
				if (hasIRType)
				{
					// IR cover shown with IR light, not flashlight
				}
				else
				{
					// Flashlight-only device might have filter
					category.AddEntry(new ACM_DynamicMenuEntry(entity, ACM_DynamicActionType.TOGGLE_IR_FILTER, deviceName));
				}
				
				// 3. Color submenu
				string flashColorTitle = "Flashlight Color";
				if (hasWeaponLaserFlashlight)
					flashColorTitle = "Laser Color";
				string flashColorLabel = flashColorTitle;
				if (!deviceName.IsEmpty())
					flashColorLabel = string.Format("%1 %2", deviceName, flashColorTitle);
				ACM_LightColorCategory flashColorCategory = new ACM_LightColorCategory(entity, flashColorLabel, false, hasWeaponLaserFlashlight);
				flashColorCategory.AddEntry(new ACM_DynamicMenuEntry(entity, ACM_DynamicActionType.SET_COLOR_WHITE, deviceName, false, hasWeaponLaserFlashlight));
				flashColorCategory.AddEntry(new ACM_DynamicMenuEntry(entity, ACM_DynamicActionType.SET_COLOR_RED, deviceName, false, hasWeaponLaserFlashlight));
				flashColorCategory.AddEntry(new ACM_DynamicMenuEntry(entity, ACM_DynamicActionType.SET_COLOR_GREEN, deviceName, false, hasWeaponLaserFlashlight));
				flashColorCategory.AddEntry(new ACM_DynamicMenuEntry(entity, ACM_DynamicActionType.SET_COLOR_BLUE, deviceName, false, hasWeaponLaserFlashlight));
				flashColorCategory.AddEntry(new ACM_DynamicMenuEntry(entity, ACM_DynamicActionType.SET_COLOR_YELLOW, deviceName, false, hasWeaponLaserFlashlight));
				flashColorCategory.AddEntry(new ACM_DynamicMenuEntry(entity, ACM_DynamicActionType.SET_COLOR_ORANGE, deviceName, false, hasWeaponLaserFlashlight));
				flashColorCategory.AddEntry(new ACM_DynamicMenuEntry(entity, ACM_DynamicActionType.SET_COLOR_CYAN, deviceName, false, hasWeaponLaserFlashlight));
				flashColorCategory.AddEntry(new ACM_DynamicMenuEntry(entity, ACM_DynamicActionType.SET_COLOR_MAGENTA, deviceName, false, hasWeaponLaserFlashlight));
				category.AddEntry(flashColorCategory);
			}
			
			// IR LIGHT: Toggle → Cover → Color
			if (hasIRType)
			{
				// 1. Device toggle
				category.AddEntry(new ACM_DynamicMenuEntry(entity, ACM_DynamicActionType.TOGGLE_IR, deviceName));
				
				// 2. Cover toggle
				category.AddEntry(new ACM_DynamicMenuEntry(entity, ACM_DynamicActionType.TOGGLE_IR_FILTER, deviceName));
				
				// 3. Color submenu
				string irColorTitle = "IR Color";
				if (hasWeaponLaserIR)
					irColorTitle = "IR Laser Color";
				string irColorLabel = irColorTitle;
				if (!deviceName.IsEmpty())
					irColorLabel = string.Format("%1 %2", deviceName, irColorTitle);
				ACM_LightColorCategory irColorCategory = new ACM_LightColorCategory(entity, irColorLabel, true, hasWeaponLaserIR);
				irColorCategory.AddEntry(new ACM_DynamicMenuEntry(entity, ACM_DynamicActionType.SET_COLOR_WHITE, deviceName, true, hasWeaponLaserIR));
				irColorCategory.AddEntry(new ACM_DynamicMenuEntry(entity, ACM_DynamicActionType.SET_COLOR_RED, deviceName, true, hasWeaponLaserIR));
				irColorCategory.AddEntry(new ACM_DynamicMenuEntry(entity, ACM_DynamicActionType.SET_COLOR_GREEN, deviceName, true, hasWeaponLaserIR));
				irColorCategory.AddEntry(new ACM_DynamicMenuEntry(entity, ACM_DynamicActionType.SET_COLOR_BLUE, deviceName, true, hasWeaponLaserIR));
				irColorCategory.AddEntry(new ACM_DynamicMenuEntry(entity, ACM_DynamicActionType.SET_COLOR_YELLOW, deviceName, true, hasWeaponLaserIR));
				irColorCategory.AddEntry(new ACM_DynamicMenuEntry(entity, ACM_DynamicActionType.SET_COLOR_ORANGE, deviceName, true, hasWeaponLaserIR));
				irColorCategory.AddEntry(new ACM_DynamicMenuEntry(entity, ACM_DynamicActionType.SET_COLOR_CYAN, deviceName, true, hasWeaponLaserIR));
				irColorCategory.AddEntry(new ACM_DynamicMenuEntry(entity, ACM_DynamicActionType.SET_COLOR_MAGENTA, deviceName, true, hasWeaponLaserIR));
				category.AddEntry(irColorCategory);
			}
		}
		
		// STROBE: Toggle → Cover → Color
		if (strobeComp)
		{
			// 1. Device toggle
			category.AddEntry(new ACM_DynamicMenuEntry(entity, ACM_DynamicActionType.TOGGLE_STROBE, deviceName));
			
			// 2. Cover toggle
			category.AddEntry(new ACM_DynamicMenuEntry(entity, ACM_DynamicActionType.TOGGLE_STROBE_IR_COVER, deviceName));
			
			// 3. Color submenu
			ACM_StrobeColorCategory strobeColorCategory = new ACM_StrobeColorCategory(entity, "Strobe Color");
			strobeColorCategory.AddEntry(new ACM_StrobeColorEntry(entity, 0, "White"));
			strobeColorCategory.AddEntry(new ACM_StrobeColorEntry(entity, 1, "Red"));
			strobeColorCategory.AddEntry(new ACM_StrobeColorEntry(entity, 2, "Green"));
			strobeColorCategory.AddEntry(new ACM_StrobeColorEntry(entity, 3, "Blue"));
			strobeColorCategory.AddEntry(new ACM_StrobeColorEntry(entity, 4, "Yellow"));
			strobeColorCategory.AddEntry(new ACM_StrobeColorEntry(entity, 5, "Orange"));
			strobeColorCategory.AddEntry(new ACM_StrobeColorEntry(entity, 6, "Cyan"));
			strobeColorCategory.AddEntry(new ACM_StrobeColorEntry(entity, 7, "Magenta"));
			category.AddEntry(strobeColorCategory);
		}
		
		ACM_NightVisionComponent nvgComp = ACM_NightVisionComponent.Cast(
			entity.FindComponent(ACM_NightVisionComponent));
		ACM_ThermalComponentV2 thermalComp = ACM_ThermalComponentV2.Cast(
			entity.FindComponent(ACM_ThermalComponentV2));
		
		ACM_TargetDetectionComponent detectionComp = ACM_TargetDetectionComponent.Cast(
			entity.FindComponent(ACM_TargetDetectionComponent));

		// An IVAS's own optics belong under SYSTEM, not in a second wedge of their own.
		//
		// The device would otherwise appear twice on the root ring - once as SYSTEM and once under
		// its item name - with the visor in one place and night vision in the other. So a device the
		// IVAS branch has already covered contributes none of those entries here; anything else on
		// the same entity, a helmet light say, still gets its category as before.
		// Capability, not control. Keyed off HasIvasVisor before, which meant that whenever authority
		// had not resolved the IVAS branch was absent AND this category un-suppressed itself, so the
		// player saw a different menu shape depending on timing. Same test the IVAS branch uses now,
		// so the two can never disagree.
		bool ivasOwned = false;
		if (detectionComp && detectionComp.HasVisorHardware())
			ivasOwned = true;

		if (nvgComp && !ivasOwned)
		{
			category.AddEntry(new ACM_DynamicMenuEntry(entity, ACM_DynamicActionType.TOGGLE_NVG, deviceName));
			
			if (nvgComp.GetAvailableColorCount() > 1)
			{
				ACM_PhosphorColorCategory phosphorCategory = new ACM_PhosphorColorCategory(entity, "Phosphor Color");
				array<string> colorNames = nvgComp.GetAvailableColorNames();
				for (int i = 0; i < colorNames.Count(); i++)
					phosphorCategory.AddEntry(new ACM_PhosphorColorEntry(entity, i, colorNames[i]));
				category.AddEntry(phosphorCategory);
			}
		}
		
		ACM_ThermalTestComponent thermalTestComp = ACM_ThermalTestComponent.Cast(
			entity.FindComponent(ACM_ThermalTestComponent));

		if (thermalTestComp && !ivasOwned)
			category.AddEntry(new ACM_DynamicMenuEntry(entity, ACM_DynamicActionType.TOGGLE_THERMAL_TEST, deviceName));

		if (thermalComp && !ivasOwned)
		{
			category.AddEntry(new ACM_DynamicMenuEntry(entity, ACM_DynamicActionType.TOGGLE_THERMAL, deviceName));
			
			// Same gate as the phosphor submenu above: a picker listing a single choice is
			// just a dead entry, so it only appears once the device carries more than one
			// palette.
			if (thermalComp.GetPaletteCount() > 1)
			{
				ACM_ThermalPaletteCategory paletteCategory = new ACM_ThermalPaletteCategory(entity, "Thermal Palette");
				array<string> paletteNames = thermalComp.GetPaletteNames();
				for (int i = 0; i < paletteNames.Count(); i++)
					paletteCategory.AddEntry(new ACM_ThermalPaletteEntry(entity, i, paletteNames[i]));
				category.AddEntry(paletteCategory);
			}
		}
		
		if (category.GetEntriesCount() > 0)
			radialMenu.AddEntry(category);
	}
	
	protected void AddEquipmentFromEntity(notnull SCR_RadialMenu radialMenu, IEntity entity, string prefix)
	{
		if (!entity)
			return;
		
		if (HasACMComponent(entity))
			AddEntityToMenu(radialMenu, entity, prefix);
		
		IEntity child = entity.GetChildren();
		while (child)
		{
			AddEquipmentFromEntity(radialMenu, child, prefix);
			child = child.GetSibling();
		}
		
		BaseInventoryStorageComponent storage = BaseInventoryStorageComponent.Cast(
			entity.FindComponent(BaseInventoryStorageComponent));
		
		if (storage)
		{
			array<IEntity> items = {};
			storage.GetAll(items);
			foreach (IEntity item : items)
			{
				if (item)
					AddEquipmentFromEntity(radialMenu, item, prefix);
			}
		}
	}
	
	protected void AddVehicleComponentsFromEntity(notnull SCR_RadialMenu radialMenu, IEntity entity, string prefix)
	{
		if (!entity)
			return;
		
		if (HasACMComponent(entity))
			AddEntityToMenu(radialMenu, entity, prefix);
		
		IEntity child = entity.GetChildren();
		while (child)
		{
			AddVehicleComponentsFromEntity(radialMenu, child, prefix);
			child = child.GetSibling();
		}
	}
	
	protected void AddWeaponAttachmentEntries(notnull SCR_RadialMenu radialMenu, notnull ChimeraCharacter character)
	{
		BaseWeaponComponent currentWeapon = SCR_WeaponLib.GetCurrentWeaponComponent(character);
		if (!currentWeapon)
			return;
		
		array<AttachmentSlotComponent> attachments = {};
		currentWeapon.GetAttachments(attachments);
		
		if (attachments.IsEmpty())
			return;
		
		foreach (AttachmentSlotComponent attachment : attachments)
		{
			IEntity entity = attachment.GetAttachedEntity();
			if (!entity)
				continue;
			
			if (!HasACMComponent(entity))
				continue;
			
			AddEntityToMenu(radialMenu, entity, "Weapon");
		}
	}
	
	protected void AddEquipmentEntries(notnull SCR_RadialMenu radialMenu, notnull ChimeraCharacter character)
	{
		EquipedLoadoutStorageComponent loadout = EquipedLoadoutStorageComponent.Cast(
			character.FindComponent(EquipedLoadoutStorageComponent));
		
		if (!loadout)
			return;
		
		// Check helmet (uses base game type)
		IEntity helmet = loadout.GetClothFromArea(LoadoutHeadCoverArea);
		if (helmet)
			AddEquipmentFromEntity(radialMenu, helmet, "Helmet");
		
		// Scan all equipped items for ACM components
		array<IEntity> allEquipped = {};
		loadout.GetAll(allEquipped);
		foreach (IEntity item : allEquipped)
		{
			if (item)
				AddEquipmentFromEntity(radialMenu, item, "Equipment");
		}
	}
	
	protected void AddVehicleEntries(notnull SCR_RadialMenu radialMenu, notnull ChimeraCharacter character)
	{
		CompartmentAccessComponent compartmentAccess = CompartmentAccessComponent.Cast(
			character.FindComponent(CompartmentAccessComponent));
		
		if (!compartmentAccess)
			return;
		
		BaseCompartmentSlot compartment = compartmentAccess.GetCompartment();
		if (!compartment)
			return;
		
		PilotCompartmentSlot pilotSlot = PilotCompartmentSlot.Cast(compartment);
		if (!pilotSlot)
			return;
		
		IEntity vehicle = compartment.GetOwner();
		if (!vehicle)
			return;
		
		while (vehicle.GetParent())
		{
			IEntity parent = vehicle.GetParent();
			if (Vehicle.Cast(parent))
				vehicle = parent;
			else
				break;
		}
		
		
		AddVehicleComponentsFromEntity(radialMenu, vehicle, "Vehicle");
	}
	
	override protected void CreateEntries(notnull SCR_RadialMenu radialMenu)
	{
		
		ChimeraCharacter character = ChimeraCharacter.Cast(SCR_PlayerController.GetLocalControlledEntity());
		if (!character)
		{
			GetGame().GetCallqueue().Call(radialMenu.Close);
			return;
		}
		
		m_AddedEntities.Clear();
		
		AddWeaponAttachmentEntries(radialMenu, character);
		AddEquipmentEntries(radialMenu, character);
		AddVehicleEntries(radialMenu, character);
		AddIvasEntries(radialMenu);
		AddTacticalRoleEntries(radialMenu);
		
		
		if (!radialMenu.GetEntryCount())
			GetGame().GetCallqueue().Call(radialMenu.Close);
	}

	//------------------------------------------------------------------------------------------------
	// ACM IVAS branch.
	//
	// THE UNIVERSAL INTERFACE. Every normal player-facing IVAS function is reachable from here with
	// a gamepad and nothing else. The two keyboard bindings that exist - remote feed toggle and
	// cycle - remain as shortcuts and are never the only way to reach their function.
	//
	// DEVICE AWARE. A control whose hardware is absent is not offered. A plain FPGPNVG has a night
	// vision component but no ACM_TargetDetectionComponent, so it gets SYSTEM with night vision and
	// phosphor in it and none of the other five branches - offering a player a Data Link switch on
	// goggles that have no data link is worse than offering nothing.
	//------------------------------------------------------------------------------------------------
	protected void AddIvasEntries(notnull SCR_RadialMenu radialMenu)
	{
		// Re-decide authority before building anything.
		//
		// The background poll gets there within a second, but a player whose menu came up without its
		// IVAS branch will close it and open it again straight away. Making that reopen the thing
		// that fixes it costs one inventory walk per device and turns a confusing dead menu into a
		// self-correcting one.
		ACM_TargetDetectionComponent.RefreshAllAuthority();

		ACM_TargetDetectionComponent device = ACM_TargetDetectionComponent.GetLocalDevice();

		// No detection hardware. Night vision and thermal keep the per-device categories they have
		// always had, so an FPGPNVG is completely unaffected by everything in this method.
		if (!device)
			return;

		AddIvasSystemCategory(radialMenu, device);
		AddIvasIffCategory(radialMenu);
		AddIvasTargetingCategory(radialMenu);
		AddIvasDataLinkCategory(radialMenu);
		AddIvasFeedCategory(radialMenu, device);
		AddIvasDisplayCategory(radialMenu);
	}

	//------------------------------------------------------------------------------------------------
	// SYSTEM: the hardware itself. Power, the visor, and the two optics.
	//
	// Night vision and thermal are here rather than under the IVAS master on purpose - they are
	// separate visual systems with their own power, and switching IVAS off must not take them with
	// it. What the master does hide is everything augmented: markers and the feed.
	//------------------------------------------------------------------------------------------------
	protected void AddIvasSystemCategory(notnull SCR_RadialMenu radialMenu, notnull ACM_TargetDetectionComponent device)
	{
		ACM_IvasCategory category = new ACM_IvasCategory(ACM_EIvasCategory.SYSTEM);

		category.AddEntry(new ACM_IvasMenuEntry(ACM_EIvasAction.IVAS_MASTER));

		IEntity owner = device.GetOwner();

		// Visor first among the hardware entries: with it stowed, the optics below have nothing to
		// show through, so the flip is the one that matters most.
		if (owner && device.HasVisorHardware())
			category.AddEntry(new ACM_IvasVisorEntry(owner));

		if (owner)
		{
			ACM_NightVisionComponent nvg = ACM_NightVisionComponent.Cast(owner.FindComponent(ACM_NightVisionComponent));
			if (nvg)
			{
				category.AddEntry(new ACM_DynamicMenuEntry(owner, ACM_DynamicActionType.TOGGLE_NVG, ""));

				// A picker listing one choice is a dead entry.
				if (nvg.GetAvailableColorCount() > 1)
				{
					ACM_PhosphorColorCategory phosphor = new ACM_PhosphorColorCategory(owner, "Phosphor Colour");
					array<string> colorNames = nvg.GetAvailableColorNames();
					for (int i = 0; i < colorNames.Count(); i++)
						phosphor.AddEntry(new ACM_PhosphorColorEntry(owner, i, colorNames[i]));

					category.AddEntry(phosphor);
				}
			}

			// The trial thermal rig, when the device carries one.
			//
			// It needs its own lookup and its own entry. It derives from ACM_NightVisionComponent,
			// so it is invisible to the thermal lookup below, and on a device that also carries a
			// plain night vision component the NVG lookup above returns THAT one - whichever sits
			// first on the entity wins. The net effect was a component present on the prefab,
			// loaded, and completely unreachable from the menu, which is indistinguishable from it
			// not being installed. Starred so nobody mistakes a test build for the shipping one.
			ACM_ThermalTestComponent thermalTest = ACM_ThermalTestComponent.Cast(owner.FindComponent(ACM_ThermalTestComponent));
			if (thermalTest)
			{
				category.AddEntry(new ACM_DynamicMenuEntry(owner, ACM_DynamicActionType.TOGGLE_THERMAL_TEST, ""));

				// Signature strength, in flight. The emat holds the precision - script setters are
				// all ints and cannot express the values that actually read - so the choice is
				// between authored materials rather than along a slider. Same gate as the phosphor
				// picker: a list of one is a dead entry.
				if (thermalTest.GetSignatureMaterialCount() > 1)
				{
					ACM_ThermalSignatureCategory sigCategory = new ACM_ThermalSignatureCategory(owner, "Signature Strength");
					array<string> sigNames = thermalTest.GetSignatureMaterialNames();
					for (int i = 0; i < sigNames.Count(); i++)
						sigCategory.AddEntry(new ACM_ThermalSignatureEntry(owner, i, sigNames[i]));

					category.AddEntry(sigCategory);
				}
			}

			ACM_ThermalComponentV2 thermal = ACM_ThermalComponentV2.Cast(owner.FindComponent(ACM_ThermalComponentV2));
			if (thermal)
			{
				category.AddEntry(new ACM_DynamicMenuEntry(owner, ACM_DynamicActionType.TOGGLE_THERMAL, ""));

				if (thermal.GetPaletteCount() > 1)
				{
					ACM_ThermalPaletteCategory palette = new ACM_ThermalPaletteCategory(owner, "Thermal Palette");
					array<string> paletteNames = thermal.GetPaletteNames();
					for (int i = 0; i < paletteNames.Count(); i++)
						palette.AddEntry(new ACM_ThermalPaletteEntry(owner, i, paletteNames[i]));

					category.AddEntry(palette);
				}
			}
		}

		// Reset is the only entry in this whole menu that asks twice.
		//
		// Everything else changes one setting and is trivially undone by selecting it again. This
		// one throws away every preference at once, and a mis-click on a thumbstick is exactly how
		// that happens by accident. The confirmation is a ring rather than a text prompt, so it
		// costs no mouse and no keyboard.
		ACM_IvasCategory reset = new ACM_IvasCategory(ACM_EIvasCategory.RESET_CONFIRMATION);
		reset.AddEntry(new ACM_IvasMenuEntry(ACM_EIvasAction.RESET_CANCEL));
		reset.AddEntry(new ACM_IvasMenuEntry(ACM_EIvasAction.RESET_CONFIRM));
		category.AddEntry(reset);

		radialMenu.AddEntry(category);
	}

	//------------------------------------------------------------------------------------------------
	// IFF: who is identified, and how loudly.
	//
	// Friendly and civilian both live here because they answer the same question - who is NOT a
	// target - and keeping them in one branch is what makes that reading obvious on the ring.
	//------------------------------------------------------------------------------------------------
	protected void AddIvasIffCategory(notnull SCR_RadialMenu radialMenu)
	{
		ACM_IvasCategory category = new ACM_IvasCategory(ACM_EIvasCategory.IFF);

		category.AddEntry(new ACM_IvasMenuEntry(ACM_EIvasAction.FRIENDLY_IFF));
		category.AddEntry(new ACM_IvasMenuEntry(ACM_EIvasAction.FRIENDLY_SCOPE));
		category.AddEntry(new ACM_IvasMenuEntry(ACM_EIvasAction.FRIENDLY_DETAIL));
		category.AddEntry(new ACM_IvasMenuEntry(ACM_EIvasAction.FRIENDLY_SILHOUETTES));
		category.AddEntry(new ACM_IvasMenuEntry(ACM_EIvasAction.CIVILIAN_IFF));
		category.AddEntry(new ACM_IvasMenuEntry(ACM_EIvasAction.UNKNOWN_CONTACTS));

		radialMenu.AddEntry(category);
	}

	//------------------------------------------------------------------------------------------------
	// TARGETING: hostiles only.
	//
	// Deliberately no "clear contacts". It manipulates store state rather than operating the device,
	// and anything shared over the link re-arrives on the next report - so from the player's side it
	// would be a button that appears not to work. The API exists for debugging and is not offered
	// here.
	//------------------------------------------------------------------------------------------------
	protected void AddIvasTargetingCategory(notnull SCR_RadialMenu radialMenu)
	{
		ACM_IvasCategory category = new ACM_IvasCategory(ACM_EIvasCategory.TARGETING);

		category.AddEntry(new ACM_IvasMenuEntry(ACM_EIvasAction.HOSTILE_TRACKING));
		category.AddEntry(new ACM_IvasMenuEntry(ACM_EIvasAction.CONTACT_FILTER));
		category.AddEntry(new ACM_IvasMenuEntry(ACM_EIvasAction.CONTACT_DETAIL));
		category.AddEntry(new ACM_IvasMenuEntry(ACM_EIvasAction.HOSTILE_SILHOUETTES));
		category.AddEntry(new ACM_IvasMenuEntry(ACM_EIvasAction.STALE_CONTACTS));

		radialMenu.AddEntry(category);
	}

	//------------------------------------------------------------------------------------------------
	// DATA LINK.
	//
	// Only offered where there is a link to control. These entries change what this client offers
	// and accepts; the server keeps deciding who may report, who may receive, and what squad scope
	// means. No toggle here can widen the player's authorised network.
	//------------------------------------------------------------------------------------------------
	protected void AddIvasDataLinkCategory(notnull SCR_RadialMenu radialMenu)
	{
		if (!ACM_TacticalContactManager.GetInstance())
			return;

		ACM_IvasCategory category = new ACM_IvasCategory(ACM_EIvasCategory.DATA_LINK);

		category.AddEntry(new ACM_IvasMenuEntry(ACM_EIvasAction.DATALINK_MASTER));
		category.AddEntry(new ACM_IvasMenuEntry(ACM_EIvasAction.DATALINK_RECEIVE));
		category.AddEntry(new ACM_IvasMenuEntry(ACM_EIvasAction.DATALINK_SHARE));
		category.AddEntry(new ACM_IvasMenuEntry(ACM_EIvasAction.DATALINK_STATUS));

		radialMenu.AddEntry(category);
	}

	//------------------------------------------------------------------------------------------------
	// REMOTE FEED. Only on hardware with a visor to project it into.
	//------------------------------------------------------------------------------------------------
	protected void AddIvasFeedCategory(notnull SCR_RadialMenu radialMenu, notnull ACM_TargetDetectionComponent device)
	{
		// Hardware test, not an authority test. The entries handle not being in control.
		if (!device.HasVisorHardware())
			return;

		ACM_IvasCategory category = new ACM_IvasCategory(ACM_EIvasCategory.REMOTE_FEED);

		category.AddEntry(new ACM_IvasMenuEntry(ACM_EIvasAction.FEED_MASTER));

		ACM_FeedSourceCategory picker = new ACM_FeedSourceCategory();
		picker.Populate();
		category.AddEntry(picker);

		category.AddEntry(new ACM_IvasMenuEntry(ACM_EIvasAction.FEED_NEXT));
		category.AddEntry(new ACM_IvasMenuEntry(ACM_EIvasAction.FEED_PREV));
		category.AddEntry(new ACM_IvasMenuEntry(ACM_EIvasAction.FEED_SIZE));
		category.AddEntry(new ACM_IvasMenuEntry(ACM_EIvasAction.FEED_POSITION));

		radialMenu.AddEntry(category);
	}

	//------------------------------------------------------------------------------------------------
	// DISPLAY: how the overlay looks, rather than what it decides.
	//------------------------------------------------------------------------------------------------
	protected void AddIvasDisplayCategory(notnull SCR_RadialMenu radialMenu)
	{
		ACM_IvasCategory category = new ACM_IvasCategory(ACM_EIvasCategory.DISPLAY);

		category.AddEntry(new ACM_IvasMenuEntry(ACM_EIvasAction.HUD));
		category.AddEntry(new ACM_IvasMenuEntry(ACM_EIvasAction.MARKER_SCALE));
		category.AddEntry(new ACM_IvasMenuEntry(ACM_EIvasAction.CONTACT_LABELS));
		category.AddEntry(new ACM_IvasMenuEntry(ACM_EIvasAction.LABEL_DISTANCE));
		category.AddEntry(new ACM_IvasMenuEntry(ACM_EIvasAction.LABEL_NAMES));

		radialMenu.AddEntry(category);
	}

	//------------------------------------------------------------------------------------------------
	// Tactical Role branch.
	//
	// Unlike every other branch in this menu, this one is PLAYER-scoped rather than gear-scoped: it
	// describes the person, not a device they are carrying, so it is added whether or not any ACM
	// hardware is equipped. That is a deliberate change in behaviour - the menu used to close itself
	// when a player had no ACM gear, and now it always has at least this one entry.
	//------------------------------------------------------------------------------------------------
	protected void AddTacticalRoleEntries(notnull SCR_RadialMenu radialMenu)
	{
		if (!ACM_TacticalRoleComponent.GetInstance())
			return;

		ACM_TacticalRoleCategory root = new ACM_TacticalRoleCategory();

		ACM_TacticalRoleGroupCategory infantry = new ACM_TacticalRoleGroupCategory("Infantry");
		AddRole(infantry, ACM_ETacticalRole.RIFLEMAN);
		AddRole(infantry, ACM_ETacticalRole.AUTOMATIC_RIFLEMAN);
		AddRole(infantry, ACM_ETacticalRole.GRENADIER);
		AddRole(infantry, ACM_ETacticalRole.MARKSMAN);
		AddRole(infantry, ACM_ETacticalRole.MEDIC);
		AddRole(infantry, ACM_ETacticalRole.COMBAT_ENGINEER);
		AddRole(infantry, ACM_ETacticalRole.SPECIAL_OPERATIONS);
		AddRole(infantry, ACM_ETacticalRole.UNASSIGNED);

		ACM_TacticalRoleGroupCategory command = new ACM_TacticalRoleGroupCategory("Command & Support");
		AddRole(command, ACM_ETacticalRole.TEAM_LEADER);
		AddRole(command, ACM_ETacticalRole.SQUAD_LEADER);
		AddRole(command, ACM_ETacticalRole.PLATOON_LEADER);
		AddRole(command, ACM_ETacticalRole.RADIO_OPERATOR);
		AddRole(command, ACM_ETacticalRole.JTAC);
		AddRole(command, ACM_ETacticalRole.UAV_OPERATOR);
		AddRole(command, ACM_ETacticalRole.CREW);
		AddRole(command, ACM_ETacticalRole.PILOT);

		root.AddEntry(infantry);
		root.AddEntry(command);

		radialMenu.AddEntry(root);
	}

	//------------------------------------------------------------------------------------------------
	protected void AddRole(ACM_TacticalRoleGroupCategory category, ACM_ETacticalRole role)
	{
		ACM_TacticalRolePreset preset = ACM_TacticalRoleCatalog.GetPreset(role);
		category.AddEntry(new ACM_TacticalRoleEntry(role, preset.m_sDisplayName));
	}
}

//------------------------------------------------------------------------------------------------
class ACM_SelectionMenuCategory : SCR_SelectionMenuCategoryEntry
{
	protected IEntity m_Owner;
	protected string m_sActionName;
	
	void ACM_SelectionMenuCategory(IEntity owner, string actionName)
	{
		if (!owner)
			return;
		
		m_sActionName = actionName;
		m_Owner = owner;
		
		string iconName = GetDeviceIcon();
		SetIcon(ACM_RADIAL_IMAGESET, iconName);
		SetName(m_sActionName);
	}
	
	protected string GetDeviceIcon()
	{
		if (!m_Owner)
			return "category";
		
		ACM_NightVisionComponent nvgComp = ACM_NightVisionComponent.Cast(m_Owner.FindComponent(ACM_NightVisionComponent));
		if (nvgComp)
			return "nvg";
		
		ACM_NVL_WeaponLaserComponent weaponLaserComp = ACM_NVL_WeaponLaserComponent.Cast(m_Owner.FindComponent(ACM_NVL_WeaponLaserComponent));
		if (weaponLaserComp)
			return "ir";
		
		ACM_NVL_StrobeComponent strobeComp = ACM_NVL_StrobeComponent.Cast(m_Owner.FindComponent(ACM_NVL_StrobeComponent));
		if (strobeComp)
			return "strobe";
		
		ACM_NVL_LightsComponent lightsComp = ACM_NVL_LightsComponent.Cast(m_Owner.FindComponent(ACM_NVL_LightsComponent));
		if (lightsComp)
		{
			if (lightsComp.HasLightType(ACM_NVL_LightType.IRLIGHT))
				return "ir";
			if (lightsComp.HasLightType(ACM_NVL_LightType.FLASHLIGHT))
				return "flashlight";
		}
		
		return "category";
	}
	
	override void Update()
	{
		SetName(m_sActionName);
	}
	
	override void SetEntryComponent(SCR_SelectionMenuEntryComponent entryComponent)
	{
		m_EntryComponent = entryComponent;
		string iconName = GetDeviceIcon();
		if (entryComponent)
		{
			Widget root = entryComponent.GetRootWidget();
			if (root)
			{
				ImageWidget iconWidget = ImageWidget.Cast(root.FindAnyWidget("Icon"));
				if (iconWidget)
					iconWidget.LoadImageFromSet(0, ACM_RADIAL_IMAGESET, iconName);
			}
		}
		Update();
	}
	
	int GetEntriesCount()
	{
		return GetEntries().Count();
	}
}

//------------------------------------------------------------------------------------------------
class ACM_LightColorCategory : SCR_SelectionMenuCategoryEntry
{
	protected IEntity m_Owner;
	protected string m_sName;
	protected bool m_bTargetsIR;
	protected bool m_bPreferWeaponLaser;
	
	protected ACM_NVL_LightsComponent ResolveLightsComponent()
	{
		if (!m_Owner)
			return null;

		array<Managed> lightComponentsManaged = {};
		m_Owner.FindComponents(ACM_NVL_LightsComponent, lightComponentsManaged);

		ACM_NVL_LightsComponent firstMatching = null;
		foreach (Managed lightManaged : lightComponentsManaged)
		{
			ACM_NVL_LightsComponent comp = ACM_NVL_LightsComponent.Cast(lightManaged);
			if (!comp)
				continue;

			if (m_bTargetsIR && !comp.HasLightType(ACM_NVL_LightType.IRLIGHT))
				continue;
			if (!m_bTargetsIR && !comp.HasLightType(ACM_NVL_LightType.FLASHLIGHT))
				continue;

			if (!firstMatching)
				firstMatching = comp;

			bool isLaserComp = ACM_NVL_WeaponLaserComponent.Cast(comp) != null;
			bool isLightComp = ACM_NVL_WeaponLightComponent.Cast(comp) != null;

			if (m_bPreferWeaponLaser && isLaserComp)
				return comp;
			if (!m_bPreferWeaponLaser && isLightComp)
				return comp;
		}

		return firstMatching;
	}
	
	void ACM_LightColorCategory(IEntity owner, string name, bool targetsIR = false, bool preferWeaponLaser = false)
	{
		if (!owner) return;
		m_sName = name;
		m_Owner = owner;
		m_bTargetsIR = targetsIR;
		m_bPreferWeaponLaser = preferWeaponLaser;
		SetIcon(ACM_RADIAL_IMAGESET, "category");
		SetName(m_sName);
	}
	
	override void Update()
	{
		ACM_NVL_LightsComponent lightsComp = ResolveLightsComponent();
		if (lightsComp)
			SetName(string.Format("%1 [%2]", m_sName, lightsComp.GetCurrentColorName()));
		else
			SetName(m_sName);
	}
	
	override void SetEntryComponent(SCR_SelectionMenuEntryComponent entryComponent)
	{
		m_EntryComponent = entryComponent;
		if (entryComponent)
		{
			Widget root = entryComponent.GetRootWidget();
			if (root)
			{
				ImageWidget iconWidget = ImageWidget.Cast(root.FindAnyWidget("Icon"));
				if (iconWidget)
					iconWidget.LoadImageFromSet(0, ACM_RADIAL_IMAGESET, "category");
			}
		}
		Update();
	}
}

//------------------------------------------------------------------------------------------------
class ACM_PhosphorColorCategory : SCR_SelectionMenuCategoryEntry
{
	protected IEntity m_Owner;
	protected string m_sName;
	
	void ACM_PhosphorColorCategory(IEntity owner, string name)
	{
		if (!owner) return;
		m_sName = name;
		m_Owner = owner;
		SetIcon(ACM_RADIAL_IMAGESET, "category");
		SetName(m_sName);
	}
	
	override void Update()
	{
		ACM_NightVisionComponent nvgComp = ACM_NightVisionComponent.Cast(m_Owner.FindComponent(ACM_NightVisionComponent));
		if (nvgComp)
			SetName(string.Format("Phosphor [%1]", nvgComp.GetCurrentColorName()));
		else
			SetName(m_sName);
	}
	
	override void SetEntryComponent(SCR_SelectionMenuEntryComponent entryComponent)
	{
		m_EntryComponent = entryComponent;
		if (entryComponent)
		{
			Widget root = entryComponent.GetRootWidget();
			if (root)
			{
				ImageWidget iconWidget = ImageWidget.Cast(root.FindAnyWidget("Icon"));
				if (iconWidget)
					iconWidget.LoadImageFromSet(0, ACM_RADIAL_IMAGESET, "category");
			}
		}
		Update();
	}
}

//------------------------------------------------------------------------------------------------
class ACM_PhosphorColorEntry : SCR_SelectionMenuEntry
{
	protected IEntity m_Owner;
	protected int m_iColorIndex;
	protected string m_sColorName;
	
	void ACM_PhosphorColorEntry(IEntity owner, int colorIndex, string colorName)
	{
		m_Owner = owner;
		m_iColorIndex = colorIndex;
		m_sColorName = colorName;
		string iconName = m_sColorName;
		iconName.ToLower();
		SetIcon(ACM_RADIAL_IMAGESET, iconName);
		SetName(m_sColorName);
	}
	
	override void Update() { SetName(m_sColorName); }
	
	override void SetEntryComponent(SCR_SelectionMenuEntryComponent entryComponent)
	{
		m_EntryComponent = entryComponent;
		if (entryComponent)
		{
			Widget root = entryComponent.GetRootWidget();
			if (root)
			{
				ImageWidget iconWidget = ImageWidget.Cast(root.FindAnyWidget("Icon"));
				if (iconWidget)
				{
					string iconName = m_sColorName;
					iconName.ToLower();
					iconWidget.LoadImageFromSet(0, ACM_RADIAL_IMAGESET, iconName);
				}
			}
		}
		Update();
	}
	
	override void OnPerform()
	{
		if (!m_Owner) return;
		ACM_NightVisionComponent nvgComp = ACM_NightVisionComponent.Cast(m_Owner.FindComponent(ACM_NightVisionComponent));
		if (nvgComp)
			nvgComp.SetPhosphorColor(m_iColorIndex);
	}
}

//------------------------------------------------------------------------------------------------
class ACM_ThermalPaletteCategory : SCR_SelectionMenuCategoryEntry
{
	protected IEntity m_Owner;
	protected string m_sName;
	
	void ACM_ThermalPaletteCategory(IEntity owner, string name)
	{
		if (!owner) return;
		m_sName = name;
		m_Owner = owner;
		SetIcon(ACM_RADIAL_IMAGESET, "category");
		SetName(m_sName);
	}
	
	override void Update()
	{
		ACM_ThermalComponentV2 thermalComp = ACM_ThermalComponentV2.Cast(m_Owner.FindComponent(ACM_ThermalComponentV2));
		if (thermalComp)
			SetName(string.Format("Palette [%1]", thermalComp.GetCurrentPaletteName()));
		else
			SetName(m_sName);
	}
	
	override void SetEntryComponent(SCR_SelectionMenuEntryComponent entryComponent)
	{
		m_EntryComponent = entryComponent;
		if (entryComponent)
		{
			Widget root = entryComponent.GetRootWidget();
			if (root)
			{
				ImageWidget iconWidget = ImageWidget.Cast(root.FindAnyWidget("Icon"));
				if (iconWidget)
					iconWidget.LoadImageFromSet(0, ACM_RADIAL_IMAGESET, "category");
			}
		}
		Update();
	}
}

//------------------------------------------------------------------------------------------------
class ACM_ThermalPaletteEntry : SCR_SelectionMenuEntry
{
	protected IEntity m_Owner;
	protected int m_iPaletteIndex;
	protected string m_sPaletteName;
	
	void ACM_ThermalPaletteEntry(IEntity owner, int paletteIndex, string paletteName)
	{
		m_Owner = owner;
		m_iPaletteIndex = paletteIndex;
		m_sPaletteName = paletteName;
		
		// Unlike the phosphor entries, the icon is NOT derived from the entry name. Palette
		// names are free text set per device in the prefab ("White Hot", "Black Hot", and
		// whatever gets added later), so lowercasing them would ask the imageset for icons
		// that do not exist. The thermal icon is correct for all of them.
		SetIcon(ACM_RADIAL_IMAGESET, "thermal");
		SetName(m_sPaletteName);
	}
	
	override void Update()
	{
		ACM_ThermalComponentV2 thermalComp = ACM_ThermalComponentV2.Cast(m_Owner.FindComponent(ACM_ThermalComponentV2));
		if (thermalComp && thermalComp.GetCurrentPaletteIndex() == m_iPaletteIndex)
			SetName(string.Format("%1 [ACTIVE]", m_sPaletteName));
		else
			SetName(m_sPaletteName);
	}
	
	override void SetEntryComponent(SCR_SelectionMenuEntryComponent entryComponent)
	{
		m_EntryComponent = entryComponent;
		if (entryComponent)
		{
			Widget root = entryComponent.GetRootWidget();
			if (root)
			{
				ImageWidget iconWidget = ImageWidget.Cast(root.FindAnyWidget("Icon"));
				if (iconWidget)
					iconWidget.LoadImageFromSet(0, ACM_RADIAL_IMAGESET, "thermal");
			}
		}
		Update();
	}
	
	override void OnPerform()
	{
		if (!m_Owner) return;
		ACM_ThermalComponentV2 thermalComp = ACM_ThermalComponentV2.Cast(m_Owner.FindComponent(ACM_ThermalComponentV2));
		if (thermalComp)
			thermalComp.SetPalette(m_iPaletteIndex);
	}
}

//------------------------------------------------------------------------------------------------
// Signature strength for the thermal test rig.
//
// A LIST of authored materials rather than a slider, because that is where the precision is. Every
// setter on ParametricMaterialInstanceComponent takes an int, so the finest step script can express
// is roughly 1/255 of a multiplier unit; an .emat takes Emissive as floats and reads correctly at
// 0.027. Choosing between materials is therefore not a workaround for a missing slider - it is the
// only control fine enough to be useful.
//------------------------------------------------------------------------------------------------
class ACM_ThermalSignatureCategory : SCR_SelectionMenuCategoryEntry
{
	protected IEntity m_Owner;
	protected string m_sName;

	void ACM_ThermalSignatureCategory(IEntity owner, string name)
	{
		if (!owner) return;
		m_sName = name;
		m_Owner = owner;
		SetIcon(ACM_RADIAL_IMAGESET, "category");
		SetName(m_sName);
	}

	override void Update()
	{
		ACM_ThermalTestComponent testComp = ACM_ThermalTestComponent.Cast(m_Owner.FindComponent(ACM_ThermalTestComponent));
		if (!testComp)
		{
			SetName(m_sName);
			return;
		}

		array<string> names = testComp.GetSignatureMaterialNames();
		int index = testComp.GetSignatureMaterialIndex();

		if (index >= 0 && index < names.Count())
			SetName(string.Format("Signature [%1]", names[index]));
		else
			SetName(m_sName);
	}

	override void SetEntryComponent(SCR_SelectionMenuEntryComponent entryComponent)
	{
		m_EntryComponent = entryComponent;
		if (entryComponent)
		{
			Widget root = entryComponent.GetRootWidget();
			if (root)
			{
				ImageWidget iconWidget = ImageWidget.Cast(root.FindAnyWidget("Icon"));
				if (iconWidget)
					iconWidget.LoadImageFromSet(0, ACM_RADIAL_IMAGESET, "category");
			}
		}
		Update();
	}
}

//------------------------------------------------------------------------------------------------
class ACM_ThermalSignatureEntry : SCR_SelectionMenuEntry
{
	protected IEntity m_Owner;
	protected int m_iIndex;
	protected string m_sLabel;

	void ACM_ThermalSignatureEntry(IEntity owner, int index, string label)
	{
		m_Owner = owner;
		m_iIndex = index;
		m_sLabel = label;

		// Same reasoning as the palette entries: these names are free text from the prefab, so
		// deriving an icon from them would ask the imageset for something that is not there.
		SetIcon(ACM_RADIAL_IMAGESET, "thermal");
		SetName(m_sLabel);
	}

	override void Update()
	{
		ACM_ThermalTestComponent testComp = ACM_ThermalTestComponent.Cast(m_Owner.FindComponent(ACM_ThermalTestComponent));
		if (testComp && testComp.GetSignatureMaterialIndex() == m_iIndex)
			SetName(string.Format("%1 [ACTIVE]", m_sLabel));
		else
			SetName(m_sLabel);
	}

	override void SetEntryComponent(SCR_SelectionMenuEntryComponent entryComponent)
	{
		m_EntryComponent = entryComponent;
		if (entryComponent)
		{
			Widget root = entryComponent.GetRootWidget();
			if (root)
			{
				ImageWidget iconWidget = ImageWidget.Cast(root.FindAnyWidget("Icon"));
				if (iconWidget)
					iconWidget.LoadImageFromSet(0, ACM_RADIAL_IMAGESET, "thermal");
			}
		}
		Update();
	}

	override void OnPerform()
	{
		if (!m_Owner) return;
		ACM_ThermalTestComponent testComp = ACM_ThermalTestComponent.Cast(m_Owner.FindComponent(ACM_ThermalTestComponent));
		if (testComp)
			testComp.SetSignatureMaterial(m_iIndex);
	}
}

//------------------------------------------------------------------------------------------------
class ACM_StrobeColorCategory : SCR_SelectionMenuCategoryEntry
{
	protected IEntity m_Owner;
	protected string m_sName;
	
	void ACM_StrobeColorCategory(IEntity owner, string name)
	{
		if (!owner) return;
		m_sName = name;
		m_Owner = owner;
		SetIcon(ACM_RADIAL_IMAGESET, "category");
		SetName(m_sName);
	}
	
	override void Update()
	{
		ACM_NVL_StrobeComponent strobeComp = ACM_NVL_StrobeComponent.Cast(m_Owner.FindComponent(ACM_NVL_StrobeComponent));
		if (strobeComp)
			SetName(string.Format("Color [%1]", strobeComp.GetCurrentColorName()));
		else
			SetName(m_sName);
	}
	
	override void SetEntryComponent(SCR_SelectionMenuEntryComponent entryComponent)
	{
		m_EntryComponent = entryComponent;
		if (entryComponent)
		{
			Widget root = entryComponent.GetRootWidget();
			if (root)
			{
				ImageWidget iconWidget = ImageWidget.Cast(root.FindAnyWidget("Icon"));
				if (iconWidget)
					iconWidget.LoadImageFromSet(0, ACM_RADIAL_IMAGESET, "category");
			}
		}
		Update();
	}
}

//------------------------------------------------------------------------------------------------
class ACM_StrobeColorEntry : SCR_SelectionMenuEntry
{
	protected IEntity m_Owner;
	protected int m_iColorIndex;
	protected string m_sColorName;
	
	void ACM_StrobeColorEntry(IEntity owner, int colorIndex, string colorName)
	{
		m_Owner = owner;
		m_iColorIndex = colorIndex;
		m_sColorName = colorName;
		string iconName = m_sColorName;
		iconName.ToLower();
		SetIcon(ACM_RADIAL_IMAGESET, iconName);
		SetName(m_sColorName);
	}
	
	override void Update() { SetName(m_sColorName); }
	
	override void SetEntryComponent(SCR_SelectionMenuEntryComponent entryComponent)
	{
		m_EntryComponent = entryComponent;
		if (entryComponent)
		{
			Widget root = entryComponent.GetRootWidget();
			if (root)
			{
				ImageWidget iconWidget = ImageWidget.Cast(root.FindAnyWidget("Icon"));
				if (iconWidget)
				{
					string iconName = m_sColorName;
					iconName.ToLower();
					iconWidget.LoadImageFromSet(0, ACM_RADIAL_IMAGESET, iconName);
				}
			}
		}
		Update();
	}
	
	override void OnPerform()
	{
		if (!m_Owner) return;
		ACM_NVL_StrobeComponent strobeComp = ACM_NVL_StrobeComponent.Cast(m_Owner.FindComponent(ACM_NVL_StrobeComponent));
		if (strobeComp)
			strobeComp.SetStrobeColor(m_iColorIndex);
	}
}

//------------------------------------------------------------------------------------------------
class ACM_GuardianAngelModeCategory : SCR_SelectionMenuCategoryEntry
{
	protected IEntity m_Owner;
	protected string m_sName;
	
	void ACM_GuardianAngelModeCategory(IEntity owner, string name)
	{
		if (!owner) return;
		m_sName = name;
		m_Owner = owner;
		SetIcon(ACM_RADIAL_IMAGESET, "category");
		SetName(m_sName);
	}
	
	override void Update()
	{
		ACM_NVL_GuardianAngelComponent guardianComp = ACM_NVL_GuardianAngelComponent.Cast(m_Owner.FindComponent(ACM_NVL_GuardianAngelComponent));
		if (guardianComp)
		{
			string modeName;
			int mode = guardianComp.GetMode();
			if (mode == 0) modeName = "Police";
			else if (mode == 1) modeName = "Fire";
			else if (mode == 2) modeName = "Ambulance";
			else if (mode == 3) modeName = "Security";
			else modeName = "Custom";
			SetName(string.Format("Mode [%1]", modeName));
		}
		else
			SetName(m_sName);
	}
	
	override void SetEntryComponent(SCR_SelectionMenuEntryComponent entryComponent)
	{
		m_EntryComponent = entryComponent;
		if (entryComponent)
		{
			Widget root = entryComponent.GetRootWidget();
			if (root)
			{
				ImageWidget iconWidget = ImageWidget.Cast(root.FindAnyWidget("Icon"));
				if (iconWidget)
					iconWidget.LoadImageFromSet(0, ACM_RADIAL_IMAGESET, "category");
			}
		}
		Update();
	}
}

//------------------------------------------------------------------------------------------------
class ACM_GuardianAngelModeEntry : SCR_SelectionMenuEntry
{
	protected IEntity m_Owner;
	protected int m_iModeIndex;
	protected string m_sModeName;
	
	void ACM_GuardianAngelModeEntry(IEntity owner, int modeIndex, string modeName)
	{
		m_Owner = owner;
		m_iModeIndex = modeIndex;
		m_sModeName = modeName;
		SetIcon(ACM_RADIAL_IMAGESET, "category");
		SetName(m_sModeName);
	}
	
	override void Update() { SetName(m_sModeName); }
	
	override void SetEntryComponent(SCR_SelectionMenuEntryComponent entryComponent)
	{
		m_EntryComponent = entryComponent;
		if (entryComponent)
		{
			Widget root = entryComponent.GetRootWidget();
			if (root)
			{
				ImageWidget iconWidget = ImageWidget.Cast(root.FindAnyWidget("Icon"));
				if (iconWidget)
					iconWidget.LoadImageFromSet(0, ACM_RADIAL_IMAGESET, "category");
			}
		}
		Update();
	}
	
	override void OnPerform()
	{
		if (!m_Owner) return;
		ACM_NVL_GuardianAngelComponent guardianComp = ACM_NVL_GuardianAngelComponent.Cast(m_Owner.FindComponent(ACM_NVL_GuardianAngelComponent));
		if (guardianComp)
			guardianComp.SetMode(m_iModeIndex);
	}
}

//------------------------------------------------------------------------------------------------
class ACM_GuardianAngelPatternCategory : SCR_SelectionMenuCategoryEntry
{
	protected IEntity m_Owner;
	protected string m_sName;
	
	void ACM_GuardianAngelPatternCategory(IEntity owner, string name)
	{
		if (!owner) return;
		m_sName = name;
		m_Owner = owner;
		SetIcon(ACM_RADIAL_IMAGESET, "category");
		SetName(m_sName);
	}
	
	override void Update()
	{
		ACM_NVL_GuardianAngelComponent guardianComp = ACM_NVL_GuardianAngelComponent.Cast(m_Owner.FindComponent(ACM_NVL_GuardianAngelComponent));
		if (guardianComp)
		{
			string patternName;
			int pattern = guardianComp.GetPattern();
			if (pattern == 0) patternName = "Alternate";
			else if (pattern == 1) patternName = "Double";
			else if (pattern == 2) patternName = "Triple";
			else if (pattern == 3) patternName = "Wig-Wag";
			else if (pattern == 4) patternName = "Steady";
			else patternName = "Quad";
			SetName(string.Format("Pattern [%1]", patternName));
		}
		else
			SetName(m_sName);
	}
	
	override void SetEntryComponent(SCR_SelectionMenuEntryComponent entryComponent)
	{
		m_EntryComponent = entryComponent;
		if (entryComponent)
		{
			Widget root = entryComponent.GetRootWidget();
			if (root)
			{
				ImageWidget iconWidget = ImageWidget.Cast(root.FindAnyWidget("Icon"));
				if (iconWidget)
					iconWidget.LoadImageFromSet(0, ACM_RADIAL_IMAGESET, "category");
			}
		}
		Update();
	}
}

//------------------------------------------------------------------------------------------------
class ACM_GuardianAngelPatternEntry : SCR_SelectionMenuEntry
{
	protected IEntity m_Owner;
	protected int m_iPatternIndex;
	protected string m_sPatternName;
	
	void ACM_GuardianAngelPatternEntry(IEntity owner, int patternIndex, string patternName)
	{
		m_Owner = owner;
		m_iPatternIndex = patternIndex;
		m_sPatternName = patternName;
		SetIcon(ACM_RADIAL_IMAGESET, "category");
		SetName(m_sPatternName);
	}
	
	override void Update() { SetName(m_sPatternName); }
	
	override void SetEntryComponent(SCR_SelectionMenuEntryComponent entryComponent)
	{
		m_EntryComponent = entryComponent;
		if (entryComponent)
		{
			Widget root = entryComponent.GetRootWidget();
			if (root)
			{
				ImageWidget iconWidget = ImageWidget.Cast(root.FindAnyWidget("Icon"));
				if (iconWidget)
					iconWidget.LoadImageFromSet(0, ACM_RADIAL_IMAGESET, "category");
			}
		}
		Update();
	}
	
	override void OnPerform()
	{
		if (!m_Owner) return;
		ACM_NVL_GuardianAngelComponent guardianComp = ACM_NVL_GuardianAngelComponent.Cast(m_Owner.FindComponent(ACM_NVL_GuardianAngelComponent));
		if (guardianComp)
			guardianComp.SetPattern(m_iPatternIndex);
	}
}

//------------------------------------------------------------------------------------------------
enum ACM_DynamicActionType
{
	TOGGLE_FLASHLIGHT,
	TOGGLE_IR,
	TOGGLE_STROBE,
	TOGGLE_IR_FILTER,
	TOGGLE_STROBE_IR_COVER,
	CYCLE_LIGHT_COLOR,
	SET_COLOR_WHITE,
	SET_COLOR_RED,
	SET_COLOR_GREEN,
	SET_COLOR_BLUE,
	SET_COLOR_YELLOW,
	SET_COLOR_ORANGE,
	SET_COLOR_CYAN,
	SET_COLOR_MAGENTA,
	TOGGLE_NVG,
	CYCLE_NVG_COLOR,
	TOGGLE_THERMAL,
	TOGGLE_THERMAL_TEST,
	SET_PHOSPHOR_DEFAULT,
	SET_PHOSPHOR_GREEN,
	SET_PHOSPHOR_WHITE,
	SET_PHOSPHOR_BLUE,
	SET_PHOSPHOR_RED,
	SET_PHOSPHOR_AMBER,
	TOGGLE_GUARDIAN_ANGEL
}

//------------------------------------------------------------------------------------------------
class ACM_DynamicMenuEntry : SCR_SelectionMenuEntry
{
	protected IEntity m_Owner;
	protected ACM_DynamicActionType m_ActionType;
	protected string m_sDeviceName;
	protected bool m_bPerformed;
	protected bool m_bColorTargetsIR;
	protected bool m_bColorPreferWeaponLaser;

	protected ACM_NVL_LightsComponent ResolveLightsComponent(bool requireFlashlight = false, bool requireIR = false, bool preferWeaponLaser = false)
	{
		if (!m_Owner)
			return null;

		array<Managed> lightComponentsManaged = {};
		m_Owner.FindComponents(ACM_NVL_LightsComponent, lightComponentsManaged);

		ACM_NVL_LightsComponent firstMatching = null;
		ACM_NVL_LightsComponent firstAny = null;

		foreach (Managed lightManaged : lightComponentsManaged)
		{
			ACM_NVL_LightsComponent comp = ACM_NVL_LightsComponent.Cast(lightManaged);
			if (!comp)
				continue;

			if (!firstAny)
				firstAny = comp;

			if (requireFlashlight && !comp.HasLightType(ACM_NVL_LightType.FLASHLIGHT))
				continue;
			if (requireIR && !comp.HasLightType(ACM_NVL_LightType.IRLIGHT))
				continue;

			if (!firstMatching)
				firstMatching = comp;

			bool isLaserComp = ACM_NVL_WeaponLaserComponent.Cast(comp) != null;
			bool isLightComp = ACM_NVL_WeaponLightComponent.Cast(comp) != null;

			if (preferWeaponLaser && isLaserComp)
				return comp;
			if (!preferWeaponLaser && isLightComp)
				return comp;
		}

		if (firstMatching)
			return firstMatching;
		if (firstAny)
			return firstAny;

		return ACM_NVL_LightsComponent.Cast(m_Owner.FindComponent(ACM_NVL_LightsComponent));
	}
	
	void ACM_DynamicMenuEntry(IEntity owner, ACM_DynamicActionType actionType, string deviceName = "", bool colorTargetsIR = false, bool colorPreferWeaponLaser = false)
	{
		if (!owner) return;
		m_Owner = owner;
		m_ActionType = actionType;
		m_sDeviceName = deviceName;
		m_bColorTargetsIR = colorTargetsIR;
		m_bColorPreferWeaponLaser = colorPreferWeaponLaser;
		SetIcon(ACM_RADIAL_IMAGESET, GetIconName());
		SetName(GetActionName());
	}
	
	protected string GetIconName()
	{
		switch (m_ActionType)
		{
			case ACM_DynamicActionType.TOGGLE_FLASHLIGHT: return "flashlight";
			case ACM_DynamicActionType.TOGGLE_IR: return "ir";
			case ACM_DynamicActionType.TOGGLE_STROBE: return "strobe";
			case ACM_DynamicActionType.TOGGLE_IR_FILTER: return "ir_cover";  // Light IR cover uses ir_cover icon
			case ACM_DynamicActionType.TOGGLE_STROBE_IR_COVER: return "strobe";  // Strobe IR cover uses strobe icon for distinction
			case ACM_DynamicActionType.SET_COLOR_WHITE: return "white";
			case ACM_DynamicActionType.SET_COLOR_RED: return "red";
			case ACM_DynamicActionType.SET_COLOR_GREEN: return "green";
			case ACM_DynamicActionType.SET_COLOR_BLUE: return "blue";
			case ACM_DynamicActionType.SET_COLOR_YELLOW: return "yellow";
			case ACM_DynamicActionType.SET_COLOR_ORANGE: return "orange";
			case ACM_DynamicActionType.SET_COLOR_CYAN: return "cyan";
			case ACM_DynamicActionType.SET_COLOR_MAGENTA: return "magenta";
			case ACM_DynamicActionType.TOGGLE_NVG: return "nvg";
			case ACM_DynamicActionType.TOGGLE_THERMAL: return "thermal";
			case ACM_DynamicActionType.TOGGLE_THERMAL_TEST: return "thermal";
			case ACM_DynamicActionType.TOGGLE_GUARDIAN_ANGEL: return "strobe";
		}
		return "category";
	}
	
	override void SetEntryComponent(SCR_SelectionMenuEntryComponent entryComponent)
	{
		m_EntryComponent = entryComponent;
		string iconName = GetIconName();
		if (entryComponent)
		{
			Widget root = entryComponent.GetRootWidget();
			if (root)
			{
				ImageWidget iconWidget = ImageWidget.Cast(root.FindAnyWidget("Icon"));
				if (iconWidget)
					iconWidget.LoadImageFromSet(0, ACM_RADIAL_IMAGESET, iconName);
			}
		}
		Update();
	}
	
	override void Update() { SetName(GetActionName()); }
	
	protected string GetActionName()
	{
		if (m_ActionType == ACM_DynamicActionType.TOGGLE_FLASHLIGHT)
		{
			ACM_NVL_LightsComponent lightsComp = ResolveLightsComponent(true, false, false);
			bool isWeaponLaser = ACM_NVL_WeaponLaserComponent.Cast(lightsComp) != null;
			if (lightsComp)
			{
				if (lightsComp.IsFlashlightOn())
				{
					if (isWeaponLaser)
						return "Laser [ON]";
					return "Flashlight [ON]";
				}
				else
				{
					if (isWeaponLaser)
						return "Laser [OFF]";
					return "Flashlight [OFF]";
				}
			}
			if (isWeaponLaser)
				return "Laser";
			return "Flashlight";
		}
		else if (m_ActionType == ACM_DynamicActionType.TOGGLE_IR)
		{
			ACM_NVL_LightsComponent lightsComp = ResolveLightsComponent(false, true, true);
			bool isWeaponLaser = ACM_NVL_WeaponLaserComponent.Cast(lightsComp) != null;
			if (lightsComp)
			{
				if (lightsComp.IsIROn())
				{
					if (isWeaponLaser)
						return "IR Laser [ON]";
					return "IR Light [ON]";
				}
				else
				{
					if (isWeaponLaser)
						return "IR Laser [OFF]";
					return "IR Light [OFF]";
				}
			}
			if (isWeaponLaser)
				return "IR Laser";
			return "IR Light";
		}
		else if (m_ActionType == ACM_DynamicActionType.TOGGLE_STROBE)
		{
			ACM_NVL_StrobeComponent strobeComp = ACM_NVL_StrobeComponent.Cast(m_Owner.FindComponent(ACM_NVL_StrobeComponent));
			if (strobeComp)
			{
				if (strobeComp.IsStrobeEnabled())
					return "Strobe [ON]";
				else
					return "Strobe [OFF]";
			}
			return "Strobe";
		}
		else if (m_ActionType == ACM_DynamicActionType.TOGGLE_IR_FILTER)
		{
			ACM_NVL_LightsComponent lightsComp = ResolveLightsComponent(false, true, true);
			if (lightsComp)
			{
				if (lightsComp.IsIRFilterRemoved())
					return "Light IR Cover [REMOVED]";
				else
					return "Light IR Cover [ON]";
			}
			return "Light IR Cover";
		}
		else if (m_ActionType == ACM_DynamicActionType.TOGGLE_STROBE_IR_COVER)
		{
			ACM_NVL_StrobeComponent strobeComp = ACM_NVL_StrobeComponent.Cast(m_Owner.FindComponent(ACM_NVL_StrobeComponent));
			if (strobeComp)
			{
				if (strobeComp.IsIRCoverRemoved())
					return "Strobe IR Cover [REMOVED]";
				else
					return "Strobe IR Cover [ON]";
			}
			return "Strobe IR Cover";
		}
		else if (m_ActionType == ACM_DynamicActionType.SET_COLOR_WHITE) return "White";
		else if (m_ActionType == ACM_DynamicActionType.SET_COLOR_RED) return "Red";
		else if (m_ActionType == ACM_DynamicActionType.SET_COLOR_GREEN) return "Green";
		else if (m_ActionType == ACM_DynamicActionType.SET_COLOR_BLUE) return "Blue";
		else if (m_ActionType == ACM_DynamicActionType.SET_COLOR_YELLOW) return "Yellow";
		else if (m_ActionType == ACM_DynamicActionType.SET_COLOR_ORANGE) return "Orange";
		else if (m_ActionType == ACM_DynamicActionType.SET_COLOR_CYAN) return "Cyan";
		else if (m_ActionType == ACM_DynamicActionType.SET_COLOR_MAGENTA) return "Magenta";
		else if (m_ActionType == ACM_DynamicActionType.TOGGLE_NVG)
		{
			ACM_NightVisionComponent nvgComp = ACM_NightVisionComponent.Cast(m_Owner.FindComponent(ACM_NightVisionComponent));
			if (nvgComp)
			{
				if (nvgComp.IsNVGActive())
					return "NVG [ON]";
				else
					return "NVG [OFF]";
			}
			return "NVG";
		}
		else if (m_ActionType == ACM_DynamicActionType.TOGGLE_THERMAL_TEST)
		{
			ACM_ThermalTestComponent testComp = ACM_ThermalTestComponent.Cast(m_Owner.FindComponent(ACM_ThermalTestComponent));
			if (testComp)
			{
				// IsNVGActive rather than a thermal-specific getter, because the activation state
				// is the inherited one - the test rig is a night vision component wearing a
				// different picture.
				if (testComp.IsNVGActive())
					return "THERMAL* [ON]";
				else
					return "THERMAL* [OFF]";
			}
			return "THERMAL*";
		}
		else if (m_ActionType == ACM_DynamicActionType.TOGGLE_THERMAL)
		{
			ACM_ThermalComponentV2 thermalComp = ACM_ThermalComponentV2.Cast(m_Owner.FindComponent(ACM_ThermalComponentV2));
			if (thermalComp)
			{
				if (thermalComp.IsThermalActive())
					return "Thermal [ON]";
				else
					return "Thermal [OFF]";
			}
			return "Thermal";
		}
		else if (m_ActionType == ACM_DynamicActionType.TOGGLE_GUARDIAN_ANGEL)
		{
			ACM_NVL_GuardianAngelComponent guardianComp = ACM_NVL_GuardianAngelComponent.Cast(m_Owner.FindComponent(ACM_NVL_GuardianAngelComponent));
			if (guardianComp)
			{
				if (guardianComp.IsEnabled())
					return "Guardian Angel [ON]";
				else
					return "Guardian Angel [OFF]";
			}
			return "Guardian Angel";
		}
		return "Unknown";
	}
	
	override void OnPerform()
	{
		if (m_bPerformed) return;
		m_bPerformed = true;
		if (!m_Owner) return;
		
		if (m_ActionType == ACM_DynamicActionType.TOGGLE_FLASHLIGHT)
		{
			ACM_NVL_LightsComponent lightsComp = ResolveLightsComponent(true, false, false);
			if (lightsComp) lightsComp.RequestToggleFlashlight();
		}
		else if (m_ActionType == ACM_DynamicActionType.TOGGLE_IR)
		{
			ACM_NVL_LightsComponent lightsComp = ResolveLightsComponent(false, true, true);
			if (lightsComp) lightsComp.RequestToggleIR();
		}
		else if (m_ActionType == ACM_DynamicActionType.TOGGLE_STROBE)
		{
			ACM_NVL_StrobeComponent strobeComp = ACM_NVL_StrobeComponent.Cast(m_Owner.FindComponent(ACM_NVL_StrobeComponent));
			if (strobeComp) strobeComp.RequestToggleStrobe();
		}
		else if (m_ActionType == ACM_DynamicActionType.TOGGLE_IR_FILTER)
		{
			ACM_NVL_LightsComponent lightsComp = ResolveLightsComponent(false, true, true);
			if (lightsComp) lightsComp.SetIRFilterRemoved(!lightsComp.IsIRFilterRemoved());
		}
		else if (m_ActionType == ACM_DynamicActionType.TOGGLE_STROBE_IR_COVER)
		{
			ACM_NVL_StrobeComponent strobeComp = ACM_NVL_StrobeComponent.Cast(m_Owner.FindComponent(ACM_NVL_StrobeComponent));
			if (strobeComp) strobeComp.SetIRCoverRemoved(!strobeComp.IsIRCoverRemoved());
		}
		else if (m_ActionType >= ACM_DynamicActionType.SET_COLOR_WHITE && m_ActionType <= ACM_DynamicActionType.SET_COLOR_MAGENTA)
		{
			int colorIndex = m_ActionType - ACM_DynamicActionType.SET_COLOR_WHITE;
			ACM_NVL_LightsComponent lightsComp = ResolveLightsComponent(!m_bColorTargetsIR, m_bColorTargetsIR, m_bColorPreferWeaponLaser);
			if (!lightsComp)
				lightsComp = ResolveLightsComponent(!m_bColorTargetsIR, m_bColorTargetsIR, !m_bColorPreferWeaponLaser);
			if (lightsComp)
				lightsComp.SetLightColor(colorIndex);
		}
		else if (m_ActionType == ACM_DynamicActionType.TOGGLE_NVG)
		{
			ACM_NightVisionComponent nvgComp = ACM_NightVisionComponent.Cast(m_Owner.FindComponent(ACM_NightVisionComponent));
			if (nvgComp) nvgComp.ToggleNVG();
		}
		else if (m_ActionType == ACM_DynamicActionType.TOGGLE_THERMAL)
		{
			ACM_ThermalComponentV2 thermalComp = ACM_ThermalComponentV2.Cast(m_Owner.FindComponent(ACM_ThermalComponentV2));
			if (thermalComp) thermalComp.ToggleThermal();
		}
		else if (m_ActionType == ACM_DynamicActionType.TOGGLE_THERMAL_TEST)
		{
			ACM_ThermalTestComponent testComp = ACM_ThermalTestComponent.Cast(m_Owner.FindComponent(ACM_ThermalTestComponent));
			if (testComp) testComp.ToggleNVG();
		}
		else if (m_ActionType == ACM_DynamicActionType.TOGGLE_GUARDIAN_ANGEL)
		{
			ACM_NVL_GuardianAngelComponent guardianComp = ACM_NVL_GuardianAngelComponent.Cast(m_Owner.FindComponent(ACM_NVL_GuardianAngelComponent));
			if (guardianComp) guardianComp.SetEnabled(!guardianComp.IsEnabled());
		}
	}
}
