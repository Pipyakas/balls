@echo off
setlocal
cd /d "%~dp0"

call source\frontends\unreal\build.bat win64
if errorlevel 1 (
    pause
    exit /b 1
)

start "" /d "%~dp0dist\win32-ue" "%~dp0dist\win32-ue\ColorBlastBalls.exe" %*
