//------------------------------------------------------------------------------------------------
// ACM_LoadoutAreas_Removed.c
// Loadout Areas removed from the combined ACM_LoadoutAreas.c file over the course of edits
// Grouped by removal reason.
//------------------------------------------------------------------------------------------------
 
// Removed: NVG references not tied to a mount
class ACM_NVG: LoadoutAreaType{};                   // File 1 only (ACM NVAL)
 
// Removed: Thermal, Raillite, Strobe
class ACM_ThermalOverlay: LoadoutAreaType{};
class ACM_Strobe: LoadoutAreaType{};

class ACM_RailLEFT: LoadoutAreaType{}; // I'm replacing Raillite general area with these two because of the loss of our merge and split script - Jacob
class ACM_RailRight: LoadoutAreaType{};

// Vlite attachment slot (used by Prefabs/Characters/Attachments/Vlite.et).
class ACM_Vlite: LoadoutAreaType{};