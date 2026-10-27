@echo off
setlocal
set "OUT_DIR=%~1"
shift

set "GAME_ARGS="
:CollectArgs
if "%~1"=="" goto Launch
set "GAME_ARGS=%GAME_ARGS% %1"
shift
goto CollectArgs

:Launch
start "" /d "%OUT_DIR%" "%OUT_DIR%\ColorBlastBalls.exe" %GAME_ARGS%
