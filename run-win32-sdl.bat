@echo off
setlocal
cd /d "%~dp0"

call build-win32-sdl.bat
if errorlevel 1 (
    pause
    exit /b 1
)

start "" /d "%~dp0dist\win32-sdl" "%~dp0dist\win32-sdl\ColorBlastBalls.exe" %*
