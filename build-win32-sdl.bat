@echo off
setlocal
cd /d "%~dp0"

set "BUILD_DIR=build\win32"
set "DIST_DIR=%~dp0dist\win32-sdl"
set "BIN_DIR=%BUILD_DIR%\bin\Release"

where cmake >nul 2>&1 || (echo CMake not found on PATH. & exit /b 1)

if not exist ".deps\cuda\sdk\include\nvrtc.h" (
    echo Fetching CUDA compiler pieces for the GPU backend...
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts\FetchCudaSdk.ps1
    if errorlevel 1 echo CUDA SDK fetch failed - building with CPU backends only.
)

if not exist "%BUILD_DIR%\CMakeCache.txt" (
    cmake -S . -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 -DBALLS_FRONTEND=sdl
    if errorlevel 1 exit /b 1
)

cmake --build "%BUILD_DIR%" --config Release --parallel --target ColorBlastBalls
if errorlevel 1 (
    echo Build failed.
    exit /b 1
)

if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"
powershell -NoProfile -Command "Get-Process ColorBlastBalls -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq '%DIST_DIR%\ColorBlastBalls.exe' } | Stop-Process -Force; Start-Sleep -Milliseconds 300"
copy /y "%BIN_DIR%\ColorBlastBalls.exe" "%DIST_DIR%\" >nul || exit /b 1
copy /y "%BIN_DIR%\*.dll" "%DIST_DIR%\" >nul || exit /b 1
robocopy "%BIN_DIR%\assets" "%DIST_DIR%\assets" /mir /njh /njs /nfl /ndl /np >nul
if errorlevel 8 exit /b 1

echo Built dist\win32-sdl\ColorBlastBalls.exe
exit /b 0
