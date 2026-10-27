@echo off
setlocal
set "OUT_DIR=%~1"
if "%OUT_DIR%"=="" (
    echo Usage: frontends\sdl\build.bat ^<output dir^>
    exit /b 1
)
cd /d "%~dp0..\.."

where cmake >nul 2>&1 || (echo CMake not found on PATH. & exit /b 1)

if not exist ".deps\cuda\sdk\include\nvrtc.h" (
    echo Fetching CUDA compiler pieces for the GPU backend...
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\FetchCudaSdk.ps1
    if errorlevel 1 echo CUDA SDK fetch failed - building with CPU backends only.
)

if not exist "build\CMakeCache.txt" (
    cmake -S . -B build -G "Visual Studio 17 2022" -A x64
    if errorlevel 1 exit /b 1
)

cmake --build build --config Release --parallel --target ColorBlastBalls
if errorlevel 1 exit /b 1

set "BIN_DIR=build\bin\Release"
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"
powershell -NoProfile -Command "Get-Process ColorBlastBalls -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq '%OUT_DIR%\ColorBlastBalls.exe' } | Stop-Process -Force; Start-Sleep -Milliseconds 300"
copy /y "%BIN_DIR%\ColorBlastBalls.exe" "%OUT_DIR%\" >nul || exit /b 1
copy /y "%BIN_DIR%\*.dll" "%OUT_DIR%\" >nul || exit /b 1
robocopy "%BIN_DIR%\assets" "%OUT_DIR%\assets" /mir /njh /njs /nfl /ndl /np >nul
if errorlevel 8 exit /b 1

echo Built %OUT_DIR%\ColorBlastBalls.exe
exit /b 0
