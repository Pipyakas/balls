@echo off
rem Builds balls_core.lib with the settings Unreal links against (/MD runtime, no whole-program optimization).
rem The profile is the scoring one unless BALLS_PROFILE=extended is set; BallsCore.Build.cs reads the same variable,
rem so the game module sees the same BALLS_FIXED_TARGET as the core it links.
setlocal EnableExtensions
if not defined BALLS_PROFILE set "BALLS_PROFILE=scoring"
set "REPO=%~dp0..\..\.."
set "OUT=%REPO%\build\win32-ue"
set "LOCK=%TEMP%\balls_core_win64.lock"
set MSBUILDDISABLENODEREUSE=1

where cmake >nul 2>&1 || (echo BuildCore: CMake not found on PATH. & exit /b 1)

rem UnrealBuildTool runs this pre-build step once per target and builds targets in parallel, so several
rem copies can reach the same CMake/MSBuild tree at once (ZERO_CHECK tlogs are not concurrency-safe).
rem Serialize them with a lock directory; a lock older than 15 minutes is treated as left by a crash.
:acquire
2>nul mkdir "%LOCK%"
if not errorlevel 1 goto locked
powershell -NoProfile -Command "if ((Test-Path -LiteralPath '%LOCK%') -and ((Get-Date) - (Get-Item -LiteralPath '%LOCK%').LastWriteTime).TotalMinutes -gt 15) { Remove-Item -LiteralPath '%LOCK%' -Recurse -Force }" >nul 2>&1
ping 127.0.0.1 -n 3 >nul
goto acquire

:locked
rem Always configure: the core's options changed (BALLS_FRONTEND replaced BALLS_BUILD_GAME), and re-running is cheap.
cmake -S "%REPO%" -B "%OUT%" -DBALLS_FRONTEND=none -DBALLS_PROFILE=%BALLS_PROFILE% -DBALLS_STATIC_CRT=OFF ^
    -DBALLS_WHOLE_PROGRAM_OPTIMIZATION=OFF
if errorlevel 1 goto failed
rem balls_core is the static library; the physics plugins (CPU, CUDA) are DLLs the core loads at run time.
cmake --build "%OUT%" --config Release --target balls_core balls_physics_plugins --parallel 1
if errorlevel 1 goto failed
rmdir "%LOCK%" 2>nul
exit /b 0

:failed
echo BuildCore: balls_core failed to build.
rmdir "%LOCK%" 2>nul
exit /b 1
