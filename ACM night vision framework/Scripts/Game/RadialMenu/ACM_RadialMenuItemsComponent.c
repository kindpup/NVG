//------------------------------------------------------------------------------------------------
// ACM_RadialMenuItemsComponent.c
// Radial menu component for ACM weapon attachments AND helmet/body equipment
// Part of ACM Night Vision and Lights mod
//------------------------------------------------------------------------------------------------

// Constants for imageset
const string ACM_RADIAL_IMAGESET = "{C258361975CC6C9D}UI/Textures/Icons/ACM_RadialMenu_Icons.imageset";

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
		
		if (nvgComp)
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
		
		if (thermalComp)
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
		
		
		if (!radialMenu.GetEntryCount())
			GetGame().GetCallqueue().Call(radialMenu.Close);
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
		else if (m_ActionType == ACM_DynamicActionType.TOGGLE_GUARDIAN_ANGEL)
		{
			ACM_NVL_GuardianAngelComponent guardianComp = ACM_NVL_GuardianAngelComponent.Cast(m_Owner.FindComponent(ACM_NVL_GuardianAngelComponent));
			if (guardianComp) guardianComp.SetEnabled(!guardianComp.IsEnabled());
		}
	}
}
