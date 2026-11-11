//------------------------------------------------------------------------------------------------
// ACM_CameraFOVReport.c
// Hands the local player's camera FOV for THIS frame to the NVG tube view.
//
// The tube cameras get the view transform from the camera handler's OnAfterCameraUpdate (exact,
// this frame). The FOV the camera entity reports at that moment is still LAST frame's: the engine
// applies the new FOV after the callback (measured: the FOV kept changing after the hook while
// aiming). In ADS the FOV moves every frame (zoom-in, focus, recoil kick), so position and zoom
// arrived one frame apart and the picture in the tubes jittered against the aim.
//
// The camera items compute the FOV in OnUpdate (pOutResult.m_fFOV), before OnAfterCameraUpdate, so
// they report it from there. Hip-fire first person and ADS are covered (the bone-transform first
// person camera calls super). Other cameras (vehicles, third person) fall back to the camera FOV.
//------------------------------------------------------------------------------------------------
modded class CharacterCameraADS
{
	override void OnUpdate(float pDt, out ScriptedCameraItemResult pOutResult)
	{
		super.OnUpdate(pDt, pOutResult);

		if (pOutResult && m_OwnerCharacter && m_OwnerCharacter == SCR_PlayerController.GetLocalControlledEntity())
			ACM_NVGTubeView.ReportCameraFOV(pOutResult.m_fFOV);
	}
}

modded class CharacterCamera1stPerson
{
	override void OnUpdate(float pDt, out ScriptedCameraItemResult pOutResult)
	{
		super.OnUpdate(pDt, pOutResult);

		if (pOutResult && m_OwnerCharacter && m_OwnerCharacter == SCR_PlayerController.GetLocalControlledEntity())
			ACM_NVGTubeView.ReportCameraFOV(pOutResult.m_fFOV);
	}
}
