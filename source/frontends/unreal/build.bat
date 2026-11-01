@echo off
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
set "STAGE_SRC=%STAGE_DIR%"
set "LAUNCHER=Balls.sh"
goto :check

:check
for %%I in ("%DIST_DIR%") do set "DIST_DIR=%%~fI"
set "SETUP_ENV=%~dp0..\..\..\build\setup-unreal-%VARIANT%.bat"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup.ps1" -Variant %VARIANT% -EnvFile "%SETUP_ENV%"
if errorlevel 1 exit /b 1
call "%SETUP_ENV%"
if defined SETUP_ONLY (
    cmd /c exit 0
    goto :eof
)
if /i "%VARIANT%"=="linux" (
    call "%~dp0BuildCoreLinux.bat"
    if errorlevel 1 (
        echo The Linux core failed to build.
        exit /b 1
    )
)

if /i "%VARIANT%"=="win64" (
    if not exist "%~dp0..\..\..\.deps\cuda\sdk\include\nvrtc.h" (
        echo Fetching CUDA compiler pieces for the GPU backend...
        powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0..\..\..\scripts\FetchCudaSdk.ps1"
        if errorlevel 1 echo CUDA SDK fetch failed - building with CPU backends only.
    )
    call "%~dp0BuildCore.bat"
    if errorlevel 1 (
        echo The core failed to build.
        exit /b 1
    )
)

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
powershell -NoProfile -Command "$dist = [IO.Path]::GetFullPath('%DIST_DIR%'); Get-Process Balls -ErrorAction SilentlyContinue | Where-Object { $_.Path -like \"$dist\*\" } | Stop-Process -Force"
robocopy "%STAGE_SRC%" "%DIST_DIR%" /mir /njh /njs /nfl /ndl /np >nul
if errorlevel 8 exit /b 1

robocopy "%~dp0..\..\..\data\assets" "%DIST_DIR%\Balls\assets" /mir /njh /njs /nfl /ndl /np >nul
if errorlevel 8 exit /b 1

echo.
echo Built: %DIST_DIR%\%LAUNCHER%
cmd /c exit 0
