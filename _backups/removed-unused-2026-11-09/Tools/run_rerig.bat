@echo off
setlocal enabledelayedexpansion
set BLENDER=
for /d %%D in ("C:\Program Files\Blender Foundation\Blender*") do (
  if exist "%%D\blender.exe" set "BLENDER=%%D\blender.exe"
)
if not defined BLENDER (
  where blender >nul 2>nul && set "BLENDER=blender"
)
if not defined BLENDER (
  echo Could not find blender.exe automatically.
  echo Edit this file and set BLENDER to your blender.exe path.
  pause
  exit /b 1
)
echo Using: !BLENDER!
"!BLENDER!" --background --factory-startup --python "%~dp0rerig_nvg_bindpose.py"
echo.
echo Done. Check renders in Tools\rerig_renders and report in Tools\rerig_report.json
pause
