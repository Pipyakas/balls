@echo off
setlocal
cd /d "%~dp0"

set "BUILD_DIR=build\win32"
set "DIST_DIR=%~dp0dist\win32-sdl"
set "BIN_DIR=%BUILD_DIR%\bin\Release"

rem Started by double-clicking, the window would close before the result could be read.
set "WAIT_AT_END="
echo %CMDCMDLINE% | find /i "%~nx0" >nul && set "WAIT_AT_END=1"

where cmake >nul 2>&1 || (echo CMake not found on PATH. & goto :failed)

if not exist ".deps\cuda\sdk\include\nvrtc.h" (
    echo Fetching CUDA compiler pieces for the GPU backend...
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts\FetchCudaSdk.ps1
    if errorlevel 1 echo CUDA SDK fetch failed - building with CPU backends only.
)

rem Configuring every time keeps the Visual Studio solution in step with renamed projects and targets.
if exist "%BUILD_DIR%\CMakeCache.txt" (
    cmake -S . -B "%BUILD_DIR%" >nul
) else (
    cmake -S . -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 -DBALLS_FRONTEND=sdl
)
if errorlevel 1 goto :failed

cmake --build "%BUILD_DIR%" --config Release --parallel --target balls
if errorlevel 1 goto :failed

if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"
powershell -NoProfile -Command "Get-Process balls -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq '%DIST_DIR%\balls.exe' } | Stop-Process -Force; Start-Sleep -Milliseconds 300"
copy /y "%BIN_DIR%\balls.exe" "%DIST_DIR%\" >nul || goto :failed
copy /y "%BIN_DIR%\*.dll" "%DIST_DIR%\" >nul || goto :failed
robocopy "%BIN_DIR%\assets" "%DIST_DIR%\assets" /mir /njh /njs /nfl /ndl /np >nul
if errorlevel 8 goto :failed

echo.
echo Built: %DIST_DIR%\balls.exe
if defined WAIT_AT_END pause
exit /b 0

:failed
echo Build failed.
if defined WAIT_AT_END pause
exit /b 1
