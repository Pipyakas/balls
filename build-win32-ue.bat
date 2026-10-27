@echo off
setlocal
cd /d "%~dp0"

rem Unreal frontend: cook and package the game into dist\win32-ue\.
rem Set UE_ROOT to override the engine fork location.
if not defined UE_ROOT set "UE_ROOT=D:\Unreal\UnrealEngine"
set "UPROJECT=%~dp0source\frontends\unreal\ColorBlastBalls.uproject"
set "DIST_DIR=%~dp0dist\win32-ue"
set "STAGE_DIR=%~dp0build\win32-ue\stage"

if not exist "%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" (
    echo Unreal Engine not found at %UE_ROOT%. Set UE_ROOT.
    exit /b 1
)

if not exist ".deps\cuda\sdk\include\nvrtc.h" (
    echo Fetching CUDA compiler pieces for the GPU backend...
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts\FetchCudaSdk.ps1
    if errorlevel 1 echo CUDA SDK fetch failed - building with CPU backends only.
)

rem The game's pre-build step (BuildCore.bat) builds balls_core and the physics plugin DLLs.
call "%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun -project="%UPROJECT%" -noP4 -platform=Win64 ^
    -clientconfig=Development -build -cook -stage -pak -archive -archivedirectory="%STAGE_DIR%" ^
    -map=/Engine/Maps/Entry -utf8output -unattended -nocompileeditor
if errorlevel 1 (
    echo Build failed.
    exit /b 1
)

if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"
powershell -NoProfile -Command "Get-Process ColorBlastBalls -ErrorAction SilentlyContinue | Where-Object { $_.Path -like '%DIST_DIR%\*' } | Stop-Process -Force; Start-Sleep -Milliseconds 300"
robocopy "%STAGE_DIR%\Windows" "%DIST_DIR%" /mir /njh /njs /nfl /ndl /np >nul
if errorlevel 8 exit /b 1

rem The core reads level files from disk; packaged builds look in <project dir>\assets.
robocopy "%~dp0data\assets" "%DIST_DIR%\ColorBlastBalls\assets" /mir /njh /njs /nfl /ndl /np >nul
if errorlevel 8 exit /b 1

echo Built dist\win32-ue\ColorBlastBalls.exe
exit /b 0
