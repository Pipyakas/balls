@echo off
setlocal EnableExtensions
rem One entry point for every front end and platform.
rem   build.bat                         the submission build: SDL + NVRHI for Windows
rem   build.bat --menu                  pick any front end and platform from a menu
rem   build.bat <frontend> [variant]    build one directly, e.g. build.bat sdl android
rem   build.bat --setup [...]           only check this PC can build it (with --menu, or a front end and variant)
rem Every build first runs its front end's setup check, which names what is missing and offers to install it.
rem Each front end owns source\frontends\<name>\build.bat; this script only dispatches to them, and the menu lists
rem whatever variants each of those reports with --variants, so a new front end needs no change here.
cd /d "%~dp0"

rem Started by double-clicking, the window would close before the result could be read.
set "WAIT_AT_END="
echo %CMDCMDLINE% | find /i "%~f0" >nul && set "WAIT_AT_END=1"
set "BALLS_NO_PAUSE=1"
setlocal EnableDelayedExpansion

set "SETUP_FLAG="
if /i "%~1"=="--setup" (set "SETUP_FLAG=--setup" & shift /1)
if "%~1"=="" (
    call source\frontends\sdl\build.bat %SETUP_FLAG%
    goto :finish
)
if /i "%~1"=="--menu" goto :menu
if /i "%~1"=="-m" goto :menu
if /i "%~1"=="--help" goto :usage
if /i "%~1"=="-h" goto :usage
if /i "%~1"=="/?" goto :usage
if not exist "source\frontends\%~1\build.bat" (
    echo Unknown front end "%~1".
    call :usage
    cmd /c exit 1
    goto :finish
)
call "source\frontends\%~1\build.bat" %SETUP_FLAG% %2
goto :finish

:menu
set /a COUNT=0
echo.
echo Build which version?
for /d %%F in (source\frontends\*) do (
    if exist "%%F\build.bat" (
        for /f "tokens=1* delims=;" %%A in ('"%%F\build.bat" --variants') do (
            set /a COUNT+=1
            set "SCRIPT_!COUNT!=%%F\build.bat"
            set "VARIANT_!COUNT!=%%A"
            echo   !COUNT!. %%~nxF %%A: %%B
        )
    )
)
if %COUNT%==0 (
    echo No front end has a build.bat.
    cmd /c exit 1
    goto :finish
)
set "CHOICE=1"
set /p "CHOICE=Number [1]: "
set /a PICKED=CHOICE 2>nul
if not defined SCRIPT_%PICKED% (
    echo "%CHOICE%" is not on the list.
    cmd /c exit 1
    goto :finish
)
echo.
call "!SCRIPT_%PICKED%!" %SETUP_FLAG% !VARIANT_%PICKED%!
goto :finish

:usage
echo Usage: build.bat [--setup] [--menu ^| ^<frontend^> [variant]]
echo   no arguments   build the submission version: SDL + NVRHI for Windows, into dist\win32-sdl
echo   --menu, -m     choose a front end and platform from a list
echo   sdl android    build one variant directly; build.bat --menu lists them
echo   --setup        only check this PC has what that build needs, offering to install anything missing
exit /b 0

:finish
set "RESULT=%ERRORLEVEL%"
if defined WAIT_AT_END pause
exit /b %RESULT%
