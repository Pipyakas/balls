@echo off
setlocal
cd /d "%~dp0"

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

cmake --build build --config Release --parallel
if errorlevel 1 (
    echo Build failed.
    exit /b 1
)
echo Build succeeded: build\bin\Release\ColorBlastBalls.exe
exit /b 0
