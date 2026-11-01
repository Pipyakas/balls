@echo off
setlocal EnableExtensions DisableDelayedExpansion
for %%I in ("%~dp0..\..\..") do set "ROOT=%%~fI"
cd /d "%ROOT%"

if /i "%~1"=="--variants" (
    echo win64;Windows - Unity player ^(Mono^) with the core's native plugins
    echo linux;Linux x86_64 - Unity IL2CPP player ^(needs the Linux .so files built on Linux in source\frontends\unity\NativeLibraries\linux-x86_64^)
    exit /b 0
)

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

set "SETUP_ENV=%ROOT%\build\setup-unity-%VARIANT%.bat"
powershell -NoProfile -ExecutionPolicy Bypass -File "%PROJECT_DIR%\setup.ps1" -Variant %VARIANT% -EnvFile "%SETUP_ENV%"
if errorlevel 1 goto :failed
call "%SETUP_ENV%"
if defined SETUP_ONLY goto :finish

if not exist ".deps\cuda\sdk\include\nvrtc.h" (
    echo Fetching CUDA compiler pieces for the GPU backend...
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts\FetchCudaSdk.ps1
    if errorlevel 1 echo CUDA SDK fetch failed - building with CPU backends only.
)

if exist "%BUILD_DIR%\CMakeCache.txt" (
    cmake -S . -B "%BUILD_DIR%" -DBALLS_FRONTEND=none -DBALLS_BUILD_UNITY_PLUGIN=ON -DBALLS_BUILD_TOOLS=OFF >nul
) else (
    cmake -S . -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 -DBALLS_FRONTEND=none -DBALLS_BUILD_UNITY_PLUGIN=ON -DBALLS_BUILD_TOOLS=OFF
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
