@echo off
setlocal EnableExtensions
set "REPO=%~dp0..\..\.."
set "OUT=%REPO%\build\win32-ue"
set "LOCK=%TEMP%\balls_core_win64.lock"
set MSBUILDDISABLENODEREUSE=1

where cmake >nul 2>&1 || (echo BuildCore: CMake not found on PATH. & exit /b 1)

:acquire
2>nul mkdir "%LOCK%"
if not errorlevel 1 goto locked
powershell -NoProfile -Command "if ((Test-Path -LiteralPath '%LOCK%') -and ((Get-Date) - (Get-Item -LiteralPath '%LOCK%').LastWriteTime).TotalMinutes -gt 15) { Remove-Item -LiteralPath '%LOCK%' -Recurse -Force }" >nul 2>&1
ping 127.0.0.1 -n 3 >nul
goto acquire

:locked
cmake -S "%REPO%" -B "%OUT%" -DBALLS_FRONTEND=none -DBALLS_STATIC_CRT=OFF ^
    -DBALLS_WHOLE_PROGRAM_OPTIMIZATION=OFF
if errorlevel 1 goto failed
cmake --build "%OUT%" --config Release --target balls_core balls_physics_plugins --parallel 1
if errorlevel 1 goto failed
rmdir "%LOCK%" 2>nul
exit /b 0

:failed
echo BuildCore: balls_core failed to build.
rmdir "%LOCK%" 2>nul
exit /b 1
