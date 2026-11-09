//------------------------------------------------------------------------------------------------
// ACM_MenuPresets.c
// Every ChimeraMenuPreset this mod adds, declared once.
//
// One block, not one per menu file. EnforceScript merges modded enums by file order rather than by
// name, and a second block for the same enum in another file is a coin flip on which one wins - so
// the mod keeps a single declaration here and every menu file refers to it. This mirrors how
// Shadow Admin declares its four presets in one place.
//
// Each name below MUST have a matching MenuPreset in Configs/System/chimeraMenus.conf. A name here
// with no preset there compiles clean and then returns null from OpenMenu at runtime, which reads
// as a dead keybind rather than as a missing config.
//------------------------------------------------------------------------------------------------
modded enum ChimeraMenuPreset
{
	ACM_LightsMenu,
	ACM_LasersMenu,
	ACM_NVGMenu,
	ACM_ThermalMenu
}
