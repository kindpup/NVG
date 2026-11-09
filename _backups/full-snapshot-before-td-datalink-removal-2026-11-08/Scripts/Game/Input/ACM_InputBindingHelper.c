// ACM Input Binding Helper
// Handles checking for custom keybinds to prevent default+custom double-triggering
//
// The Enfusion input system fires action listeners for ALL bound keys (default + custom).
// This helper checks if the default key was pressed and blocks it if a custom binding exists.

class ACM_InputBindingHelper
{
	// Map of action names to their default keyboard input strings
	protected static ref map<string, string> s_mDefaultInputs;
	
	// Static map to track evidence of custom bindings
	protected static ref map<string, bool> s_mHasCustomBinding;
	protected static ref map<string, float> s_mLastNonDefaultTrigger;
	
	//------------------------------------------------------------------------------------------------
	// Initialize default bindings map - these match chimeraInputCommon.conf
	//------------------------------------------------------------------------------------------------
	protected static void Init()
	{
		if (s_mDefaultInputs)
			return;
		
		s_mDefaultInputs = new map<string, string>();
		s_mHasCustomBinding = new map<string, bool>();
		s_mLastNonDefaultTrigger = new map<string, float>();
		
		// NVG actions - using input device strings
		s_mDefaultInputs.Set("ACM_NV_Toggle", "keyboard:KC_N");
		s_mDefaultInputs.Set("ACM_NV_AutoGain", "keyboard:KC_G");
		s_mDefaultInputs.Set("ACM_NV_Palette", "keyboard:KC_N");
		s_mDefaultInputs.Set("ACM_NV_Reset", "keyboard:KC_R");
		
		// Thermal actions
		s_mDefaultInputs.Set("ACM_Thermal_Toggle", "keyboard:KC_N");
		s_mDefaultInputs.Set("ACM_Thermal_Palette", "keyboard:KC_N");
		
		// Light actions
		s_mDefaultInputs.Set("ACMToggleFlashlight", "keyboard:KC_L");
		s_mDefaultInputs.Set("ACMToggleIR", "keyboard:KC_SEMICOLON");
		s_mDefaultInputs.Set("ACMStrobe", "keyboard:KC_APOSTROPHE");
		s_mDefaultInputs.Set("ACM_Light_BrightnessUp", "keyboard:KC_RBRACKET");
		s_mDefaultInputs.Set("ACM_Light_BrightnessDown", "keyboard:KC_LBRACKET");
		s_mDefaultInputs.Set("ACM_OpenRadialMenu", "keyboard:KC_COMMA");
		
		// Guardian Angel
		s_mDefaultInputs.Set("ACMToggleGuardianAngel", "keyboard:KC_K");
		s_mDefaultInputs.Set("ACMGuardianAngelMode", "keyboard:KC_K");
		s_mDefaultInputs.Set("ACMGuardianAngelPattern", "keyboard:KC_K");
	}
	
	//------------------------------------------------------------------------------------------------
	// Check if we should handle this input
	// Returns TRUE if we should process the action
	// Returns FALSE if we should ignore it (default key pressed but custom binding exists)
	//------------------------------------------------------------------------------------------------
	static bool ShouldHandleInput(string actionName)
	{
		Init();
		
		InputManager im = GetGame().GetInputManager();
		if (!im)
			return true;
		
		// Get our known default input for this action
		string defaultInput;
		if (!s_mDefaultInputs.Find(actionName, defaultInput))
			return true; // Unknown action, allow it
		
		// Check if the default input device/key is currently active
		float defaultValue = im.GetActionValue(defaultInput);
		bool defaultPressed = (defaultValue > 0.5);
		
		// Get the action value to see if the action is being triggered
		float actionValue = im.GetActionValue(actionName);
		bool actionTriggered = (actionValue > 0.5);
		
		// If action is triggered but default input is NOT active,
		// that means a custom key triggered it - record this as evidence and allow it
		if (actionTriggered && !defaultPressed)
		{
			s_mHasCustomBinding.Set(actionName, true);
			s_mLastNonDefaultTrigger.Set(actionName, System.GetTickCount());
			return true;
		}
		
		// If default IS pressed, check if we have evidence of a custom binding
		if (defaultPressed)
		{
			bool hasEvidence;
			if (s_mHasCustomBinding.Find(actionName, hasEvidence) && hasEvidence)
			{
				// Verify the evidence is recent (within last 60 seconds)
				float lastTime;
				if (s_mLastNonDefaultTrigger.Find(actionName, lastTime))
				{
					float elapsed = System.GetTickCount() - lastTime;
					if (elapsed < 60000) // 60 seconds
					{
						// User has a custom binding AND is pressing default key - block it
						return false;
					}
					else
					{
						// Evidence is stale, clear it
						s_mHasCustomBinding.Set(actionName, false);
					}
				}
			}
		}
		
		return true;
	}
	
	//------------------------------------------------------------------------------------------------
	// Manually mark an action as having a custom binding
	// Call this if you know the user has set a custom keybind
	//------------------------------------------------------------------------------------------------
	static void MarkHasCustomBinding(string actionName)
	{
		Init();
		s_mHasCustomBinding.Set(actionName, true);
		s_mLastNonDefaultTrigger.Set(actionName, System.GetTickCount());
	}
	
	//------------------------------------------------------------------------------------------------
	// Clear custom binding evidence for an action
	//------------------------------------------------------------------------------------------------
	static void ClearCustomBinding(string actionName)
	{
		Init();
		s_mHasCustomBinding.Set(actionName, false);
		s_mLastNonDefaultTrigger.Remove(actionName);
	}
	
	//------------------------------------------------------------------------------------------------
	// Clear all tracking data
	//------------------------------------------------------------------------------------------------
	static void ClearAll()
	{
		if (s_mHasCustomBinding)
			s_mHasCustomBinding.Clear();
		if (s_mLastNonDefaultTrigger)
			s_mLastNonDefaultTrigger.Clear();
	}
}
