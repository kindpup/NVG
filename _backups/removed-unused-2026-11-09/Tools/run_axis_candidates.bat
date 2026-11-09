@echo off
setlocal enabledelayedexpansion
set BLENDER=
for /d %%D in ("C:\Program Files\Blender Foundation\Blender*") do (
  if exist "%%D\blender.exe" set "BLENDER=%%D\blender.exe"
)
if not defined BLENDER ( where blender >nul 2>nul && set "BLENDER=blender" )
if not defined BLENDER ( echo Could not find blender.exe & pause & exit /b 1 )
"!BLENDER!" --background --factory-startup --python "%~dp0axis_candidates.py"
echo.
echo Done - see Tools\axis_candidates
pause
