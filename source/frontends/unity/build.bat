@echo off
setlocal EnableExtensions DisableDelayedExpansion
rem Builds the Unity front end's native plugins and player, then stages a runnable copy under dist\.
rem   build.bat               the Windows player (win64)
rem   build.bat <variant>     one of the variants below
rem   build.bat --variants    lists them as id;description, which the root build.bat menu reads
rem   build.bat --setup [variant]   only checks this PC can build it, naming what is missing
rem The Unity project lives in this folder. The editor is found from UNITY_EXE, else from the version in
rem its ProjectVersion.txt under C:\Unity or the Unity Hub's install folder.
for %%I in ("%~dp0..\..\..") do set "ROOT=%%~fI"
cd /d "%ROOT%"

if /i "%~1"=="--variants" (
    echo win64;Windows - Unity player ^(Mono^) with the core's native plugins
    echo linux;Linux x86_64 - Unity IL2CPP player ^(needs the Linux .so files built on Linux in source\frontends\unity\NativeLibraries\linux-x86_64^)
    exit /b 0
)

rem Started by double-clicking, the window would close before the result could be read. The root build.bat sets
rem BALLS_NO_PAUSE because it waits on its own.
set "WAIT_AT_END="
if not defined BALLS_NO_PAUSE echo %CMDCMDLINE% | find /i "%~f0" >nul && set "WAIT_AT_END=1"

set "SETUP_ONLY="
if /i "%~1"=="--setup" (set "SETUP_ONLY=1" & shift /1)
set "VARIANT=%~1"
if not defined VARIANT set "VARIANT=win64"
set "PROJECT_DIR=%ROOT%\source\frontends\unity"
set "BUILD_DIR=build\win32-unity"
set "PLAYER_NAME="
if /i "%VARIANT%"=="win64" (
    set "BUILD_METHOD=Balls.Editor.BallsBuilder.BuildWindows"
    set "PLAYER_NAME=ColorBlastBallsUnity.exe"
    set "PLAYBACK_ENGINE=WindowsStandaloneSupport"
    set "STAGE_DIR=%ROOT%\build\win32-unity\player-win64"
    set "DIST_DIR=%ROOT%\dist\win32-unity"
)
if /i "%VARIANT%"=="linux" (
    set "BUILD_METHOD=Balls.Editor.BallsBuilder.BuildLinux"
    set "PLAYER_NAME=ColorBlastBallsUnity.x86_64"
    set "PLAYBACK_ENGINE=LinuxStandaloneSupport"
    set "STAGE_DIR=%ROOT%\build\win32-unity\player-linux"
    set "DIST_DIR=%ROOT%\dist\linux-unity"
)
if not defined PLAYER_NAME (
    echo Unknown Unity variant "%VARIANT%". Known variants:
    call "%~f0" --variants
    goto :failed
)

echo Checking this PC can build the Unity %VARIANT% player:
set "MISSING="
if not defined UNITY_EXE (
    for /f "tokens=2" %%V in ('findstr /b /c:"m_EditorVersion:" "%PROJECT_DIR%\ProjectSettings\ProjectVersion.txt"') do set "UNITY_VERSION=%%V"
)
if not defined UNITY_EXE if exist "C:\Unity\%UNITY_VERSION%\Editor\Unity.exe" set "UNITY_EXE=C:\Unity\%UNITY_VERSION%\Editor\Unity.exe"
if not defined UNITY_EXE if exist "%ProgramFiles%\Unity\Hub\Editor\%UNITY_VERSION%\Editor\Unity.exe" set "UNITY_EXE=%ProgramFiles%\Unity\Hub\Editor\%UNITY_VERSION%\Editor\Unity.exe"
if defined UNITY_EXE (
    echo   [ ok ] Unity                  %UNITY_EXE%
) else (
    echo   [MISS] Unity %UNITY_VERSION%      install it with the Unity Hub, or set UNITY_EXE to its Unity.exe
    set "MISSING=1"
)
if defined UNITY_EXE for %%E in ("%UNITY_EXE%") do set "PLAYBACK_DIR=%%~dpEData\PlaybackEngines\%PLAYBACK_ENGINE%"
if defined UNITY_EXE (
    if exist "%PLAYBACK_DIR%" (
        echo   [ ok ] %PLAYBACK_ENGINE%
    ) else (
        echo   [MISS] %PLAYBACK_ENGINE%  add the build support module to Unity %UNITY_VERSION% in the Unity Hub
        set "MISSING=1"
    )
)
where cmake >nul 2>&1 && (
    echo   [ ok ] CMake                  builds the native plugins
) || (
    echo   [MISS] CMake                  install CMake 3.25+ or run a Visual Studio developer prompt
    set "MISSING=1"
)
if /i "%VARIANT%"=="linux" (
    if exist "%PROJECT_DIR%\NativeLibraries\linux-x86_64\libballs_unity.so" (
        echo   [ ok ] Linux native plugins   source\frontends\unity\NativeLibraries\linux-x86_64
    ) else (
        echo   [MISS] Linux native plugins   build them on Linux: cmake -S . -B build/linux-unity -DBALLS_FRONTEND=none -DBALLS_PROFILE=extended
        echo                                  then copy build/linux-unity/bin/*.so into source\frontends\unity\NativeLibraries\linux-x86_64
        set "MISSING=1"
    )
)
if defined MISSING goto :failed
if defined SETUP_ONLY (
    echo Ready to build the Unity %VARIANT% player.
    goto :finish
)

if not exist ".deps\cuda\sdk\include\nvrtc.h" (
    echo Fetching CUDA compiler pieces for the GPU backend...
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts\FetchCudaSdk.ps1
    if errorlevel 1 echo CUDA SDK fetch failed - building with CPU backends only.
)

rem The Windows DLLs are built for both variants: the editor loads them while it builds the player.
if exist "%BUILD_DIR%\CMakeCache.txt" (
    cmake -S . -B "%BUILD_DIR%" -DBALLS_FRONTEND=none -DBALLS_PROFILE=extended -DBALLS_BUILD_UNITY_PLUGIN=ON -DBALLS_BUILD_TOOLS=OFF >nul
) else (
    cmake -S . -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 -DBALLS_FRONTEND=none -DBALLS_PROFILE=extended -DBALLS_BUILD_UNITY_PLUGIN=ON -DBALLS_BUILD_TOOLS=OFF
)
if errorlevel 1 goto :failed
cmake --build "%BUILD_DIR%" --config Release --parallel
if errorlevel 1 goto :failed

set "BALLS_UNITY_NATIVE_DIR=%ROOT%\%BUILD_DIR%\bin\Release"
set "UNITY_LOG=%ROOT%\%BUILD_DIR%\unity-%VARIANT%.log"
if exist "%STAGE_DIR%" rmdir /s /q "%STAGE_DIR%"
echo Building the Unity %VARIANT% player (log: %UNITY_LOG%)...
start "" /wait "%UNITY_EXE%" -batchmode -quit -projectPath "%PROJECT_DIR%" -executeMethod %BUILD_METHOD% -balls-player-path "%STAGE_DIR%\%PLAYER_NAME%" -logFile "%UNITY_LOG%"
set "UNITY_RESULT=%ERRORLEVEL%"
rem Opening the project removes the import settings of plugins that are absent on this PC (the Linux .so files on
rem Windows); they are tracked, so they are put back.
where git >nul 2>&1 && git checkout -- source/frontends/unity/Assets/Plugins source/frontends/unity/Assets/StreamingAssets 2>nul
if not "%UNITY_RESULT%"=="0" (
    echo Unity exited with %UNITY_RESULT%; see %UNITY_LOG%.
    goto :failed
)
if not exist "%STAGE_DIR%\%PLAYER_NAME%" (
    echo Unity reported success but wrote no player; see %UNITY_LOG%.
    goto :failed
)

if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"
powershell -NoProfile -Command "Get-Process ColorBlastBallsUnity -ErrorAction SilentlyContinue | Where-Object { $_.Path -like '%DIST_DIR%\*' } | Stop-Process -Force; Start-Sleep -Milliseconds 300"
rem Burst writes its debug symbols beside the player in a folder it names DoNotShip.
robocopy "%STAGE_DIR%" "%DIST_DIR%" /mir /xd *_DoNotShip /njh /njs /nfl /ndl /np >nul
if errorlevel 8 goto :failed

echo.
echo Built: %DIST_DIR%\%PLAYER_NAME%
:finish
if defined WAIT_AT_END pause
exit /b 0

:failed
echo Build failed.
if defined WAIT_AT_END pause
exit /b 1
