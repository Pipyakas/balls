@echo off
rem Cooks and packages the Unreal front end, then stages a runnable copy under dist\.
rem   build.bat               the main variant (win64 on Windows)
rem   build.bat <variant>     one of the variants below
rem   build.bat --variants    lists them as id;description, which the root build.bat menu reads
rem   build.bat --setup [variant]   only checks this PC can build it, naming what is missing
rem The engine fork is expected at UE_ROOT (default D:\Unreal\UnrealEngine); its Linux toolchain at
rem LINUX_MULTIARCH_ROOT (default D:\UnrealToolchains\v26_clang-20.1.8-rockylinux8). The .uproject pre-build step
rem builds the core and the physics plugins for the target platform before UAT cooks.
cd /d "%~dp0"

set "DEFAULT_VARIANT=win64"
if /i "%~1"=="--variants" (
    echo win64;Windows - packaged Win64 game ^(needs the engine fork^)
    echo linux;Linux x86_64 - packaged game cross-compiled on Windows ^(needs the engine fork and its Linux toolchain^)
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
cmd /c exit 1
goto :eof

:win64
set "PLATFORM=Win64"
set "STAGE_SUBDIR=Windows"
set "DIST_DIR=%~dp0..\..\..\dist\win32-ue"
set "STAGE_DIR=%~dp0..\..\..\build\win32-ue\stage"
set "LAUNCHER=ColorBlastBalls.exe"
goto :check

:linux
set "PLATFORM=Linux"
set "STAGE_SUBDIR=Linux"
set "DIST_DIR=%~dp0..\..\..\dist\linux-ue"
set "STAGE_DIR=%~dp0..\..\..\build\linux-ue\stage"
set "LAUNCHER=ColorBlastBalls.sh"
goto :check

:check
if not defined UE_ROOT set "UE_ROOT=D:\Unreal\UnrealEngine"
if not exist "%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" (
    echo Unreal Engine not found at "%UE_ROOT%". Set UE_ROOT to the engine fork.
    cmd /c exit 1
    goto :eof
)
if /i "%VARIANT%"=="linux" (
    if not defined LINUX_MULTIARCH_ROOT set "LINUX_MULTIARCH_ROOT=D:\UnrealToolchains\v26_clang-20.1.8-rockylinux8\"
    if not exist "%LINUX_MULTIARCH_ROOT%x86_64-unknown-linux-gnu\bin\clang++.exe" (
        echo Unreal's Linux toolchain not found at "%LINUX_MULTIARCH_ROOT%".
        echo Install it from Epic's Linux cross-compile toolchain, or set LINUX_MULTIARCH_ROOT.
        cmd /c exit 1
        goto :eof
    )
)
where cmake >nul 2>&1 || (
    echo CMake not found on PATH; the core builds through CMake.
    cmd /c exit 1
    goto :eof
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
)

call "%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun ^
    -project="%~dp0ColorBlastBalls.uproject" -noP4 -platform=%PLATFORM% ^
    -clientconfig=Development -build -cook -stage -pak -archive -archivedirectory="%STAGE_DIR%" ^
    -map=/Engine/Maps/Entry -utf8output -unattended -nocompileeditor -nullrhi
if errorlevel 1 (
    echo Build failed.
    cmd /c exit 1
    goto :eof
)

if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"
powershell -NoProfile -Command "Get-Process ColorBlastBalls -ErrorAction SilentlyContinue | Stop-Process -Force"
robocopy "%STAGE_DIR%\%STAGE_SUBDIR%" "%DIST_DIR%" /mir /njh /njs /nfl /ndl /np >nul
if errorlevel 8 (cmd /c exit 1 & goto :eof)

rem The core reads level files from disk; packaged builds look in <project dir>\assets.
robocopy "%~dp0..\..\..\data\assets" "%DIST_DIR%\ColorBlastBalls\assets" /mir /njh /njs /nfl /ndl /np >nul
if errorlevel 8 (cmd /c exit 1 & goto :eof)

echo.
echo Built: %DIST_DIR%\%LAUNCHER%
cmd /c exit 0
