@echo off
setlocal
cd /d "%~dp0"

set "FRONTEND=sdl"
if not "%~1"=="" if exist "frontends\%~1\run.bat" (
    set "FRONTEND=%~1"
    shift
)

set "GAME_ARGS="
:CollectArgs
if "%~1"=="" goto Launch
set "GAME_ARGS=%GAME_ARGS% %1"
shift
goto CollectArgs

:Launch
call build.bat %FRONTEND%
if errorlevel 1 (
    pause
    exit /b 1
)

call "frontends\%FRONTEND%\run.bat" "%~dp0out\%FRONTEND%" %GAME_ARGS%
