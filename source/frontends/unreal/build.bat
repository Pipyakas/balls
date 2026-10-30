@echo off
rem Cooks and packages the Unreal front end, then stages a runnable copy under dist\.
rem   build.bat               the main variant (win64 on Windows)
rem   build.bat <variant>     one of the variants below
rem   build.bat --variants    lists them as id;description, which the root build.bat menu reads
rem   build.bat --setup [variant]   only checks this PC can build it, naming what is missing
rem Unreal Engine 5.8 (a stock install or the fork) is taken from UE_ROOT, else found by FindUnrealEngine.ps1; its
rem Linux toolchain at LINUX_MULTIARCH_ROOT (default D:\UnrealToolchains\v26_clang-20.1.8-rockylinux8). The .uproject
rem pre-build step builds the core and the physics plugins for the target platform before UAT cooks.
cd /d "%~dp0"

set "DEFAULT_VARIANT=win64"
if /i "%~1"=="--variants" (
    echo win64;Windows - packaged Win64 game ^(needs Unreal Engine 5.8^)
    echo linux;Linux x86_64 - packaged game cross-compiled on Windows ^(needs Unreal Engine 5.8 and its Linux toolchain^)
    cmd /c exit 0
    goto :eof
)

set "SETUP_ONLY="
if /i "%~1"=="--setup" (set "SETUP_ONLY=1" & shift /1)
set "VARIANT=%~1"
if not defined VARIANT set "VARIANT=%DEFAULT_VARIANT%"

if /i "%VARIANT%"=="win64" goto :win64
if /i "%VARIANT%"=="linux" goto :linux
echo Unknown Unreal variant "%VARIANT%". Known variants:
call "%~f0" --variants
exit /b 1

:win64
set "PLATFORM=Win64"
set "DIST_DIR=%~dp0..\..\..\dist\win32-ue"
set "STAGE_DIR=%~dp0..\..\..\build\win32-ue\stage"
set "STAGE_SRC=%STAGE_DIR%\Windows"
set "LAUNCHER=Balls.exe"
goto :check

:linux
set "PLATFORM=Linux"
set "DIST_DIR=%~dp0..\..\..\dist\linux-ue"
set "STAGE_DIR=%~dp0..\..\..\build\linux-ue\stage"
rem The Linux archive has no platform folder: the launcher and the game folder sit at the archive root.
set "STAGE_SRC=%STAGE_DIR%"
set "LAUNCHER=Balls.sh"
goto :check

:check
rem Printed at the end, so it is made absolute: a path with ..\ in it does not open from every tool.
for %%I in ("%DIST_DIR%") do set "DIST_DIR=%%~fI"
if not defined UE_ROOT (
    for /f "usebackq delims=" %%R in (`powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0FindUnrealEngine.ps1"`) do set "UE_ROOT=%%R"
)
if not defined UE_ROOT (
    echo Unreal Engine 5.8 not found. Set UE_ROOT to its folder, the one that contains Engine\.
    exit /b 1
)
if not exist "%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" (
    echo Unreal Engine not found at "%UE_ROOT%". Set UE_ROOT to an Unreal Engine 5.8 install.
    exit /b 1
)
if /i "%VARIANT%"=="linux" (
    if not defined LINUX_MULTIARCH_ROOT set "LINUX_MULTIARCH_ROOT=D:\UnrealToolchains\v26_clang-20.1.8-rockylinux8\"
    if not exist "%LINUX_MULTIARCH_ROOT%x86_64-unknown-linux-gnu\bin\clang++.exe" (
        echo Unreal's Linux toolchain not found at "%LINUX_MULTIARCH_ROOT%".
        echo Install it from Epic's Linux cross-compile toolchain, or set LINUX_MULTIARCH_ROOT.
        exit /b 1
    )
    rem UnrealBuildTool does not run the .uproject pre-build steps for a cross-compiled target, so the core and the
    rem plugins are built here, with the same toolchain, before UAT links the game against them.
    call "%~dp0BuildCoreLinux.bat"
    if errorlevel 1 (
        echo The Linux core failed to build.
        exit /b 1
    )
)
where cmake >nul 2>&1 || (
    echo CMake not found on PATH; the core builds through CMake.
    exit /b 1
)
if defined SETUP_ONLY (
    echo %VARIANT%: everything needed is present.
    cmd /c exit 0
    goto :eof
)

if /i "%VARIANT%"=="win64" (
    if not exist "%~dp0..\..\..\.deps\cuda\sdk\include\nvrtc.h" (
        echo Fetching CUDA compiler pieces for the GPU backend...
        powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\..\scripts\FetchCudaSdk.ps1"
        if errorlevel 1 echo CUDA SDK fetch failed - building with CPU backends only.
    )
    rem BallsCore.Build.cs stages the physics plugins only if they exist when UnrealBuildTool reads it, which is
    rem before the .uproject pre-build step builds them, so a fresh clone would package a game without them.
    call "%~dp0BuildCore.bat"
    if errorlevel 1 (
        echo The core failed to build.
        exit /b 1
    )
)

rem The archive keeps whatever earlier runs put there, such as a game staged under an older project name, and
rem dist is mirrored from it.
if exist "%STAGE_DIR%" rmdir /s /q "%STAGE_DIR%"
call "%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun ^
    -project="%~dp0Balls.uproject" -noP4 -platform=%PLATFORM% ^
    -clientconfig=Development -build -cook -stage -pak -archive -archivedirectory="%STAGE_DIR%" ^
    -map=/Engine/Maps/Entry -utf8output -unattended -nullrhi
if errorlevel 1 (
    echo Build failed.
    exit /b 1
)

if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"
rem Only the copy staged here: the SDL game is balls.exe too, and process names ignore case.
powershell -NoProfile -Command "$dist = [IO.Path]::GetFullPath('%DIST_DIR%'); Get-Process Balls -ErrorAction SilentlyContinue | Where-Object { $_.Path -like \"$dist\*\" } | Stop-Process -Force"
robocopy "%STAGE_SRC%" "%DIST_DIR%" /mir /njh /njs /nfl /ndl /np >nul
if errorlevel 8 exit /b 1

rem The core reads level files from disk; packaged builds look in <project dir>\assets.
robocopy "%~dp0..\..\..\data\assets" "%DIST_DIR%\Balls\assets" /mir /njh /njs /nfl /ndl /np >nul
if errorlevel 8 exit /b 1

echo.
echo Built: %DIST_DIR%\%LAUNCHER%
cmd /c exit 0
