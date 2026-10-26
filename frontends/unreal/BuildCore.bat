@echo off
rem Builds balls_core.lib with the settings Unreal links against (/MD runtime, no LTO).
setlocal
set "REPO=%~dp0..\.."
set "OUT=%REPO%\build-unreal"

where cmake >nul 2>&1 || (echo BuildCore: CMake not found on PATH. & exit /b 1)

if not exist "%OUT%\CMakeCache.txt" (
    cmake -S "%REPO%" -B "%OUT%" -DBALLS_BUILD_GAME=OFF -DBALLS_STATIC_CRT=OFF -DBALLS_LTO=OFF
    if errorlevel 1 exit /b 1
)
cmake --build "%OUT%" --config Release --target balls_core --parallel
if errorlevel 1 (
    echo BuildCore: balls_core failed to build.
    exit /b 1
)
exit /b 0
