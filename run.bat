@echo off
setlocal
cd /d "%~dp0"

call build.bat
if errorlevel 1 (
    pause
    exit /b 1
)

set "RUN_DIR=%~dp0out\run"
if not exist "%RUN_DIR%" mkdir "%RUN_DIR%"
copy /y "build\bin\Release\ColorBlastBalls.exe" "%RUN_DIR%\ColorBlastBalls.exe" >nul
if errorlevel 1 (
    echo Could not replace out\run\ColorBlastBalls.exe - close the running game and try again.
    pause
    exit /b 1
)

start "" "%RUN_DIR%\ColorBlastBalls.exe" %*
