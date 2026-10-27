@echo off
setlocal
cd /d "%~dp0"
set "ROOT=%~dp0"

set "FRONTEND=%~1"
if "%FRONTEND%"=="" set "FRONTEND=all"

if /i not "%FRONTEND%"=="all" (
    call :BuildFrontend "%FRONTEND%"
    exit /b %errorlevel%
)

set "BUILT=0"
for /d %%F in (frontends\*) do (
    if exist "%%F\build.bat" (
        call :BuildFrontend "%%~nxF" || exit /b 1
        set "BUILT=1"
    )
)
if "%BUILT%"=="0" (
    echo No front end has a build.bat under frontends\.
    exit /b 1
)
exit /b 0

:BuildFrontend
if not exist "frontends\%~1\build.bat" (
    echo Unknown front end "%~1": frontends\%~1\build.bat not found.
    exit /b 1
)
echo === Building %~1 into out\%~1 ===
call "frontends\%~1\build.bat" "%ROOT%out\%~1"
if errorlevel 1 (
    echo Build of %~1 failed.
    exit /b 1
)
exit /b 0
