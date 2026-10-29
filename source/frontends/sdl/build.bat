@echo off
setlocal EnableExtensions DisableDelayedExpansion
rem Builds one variant of the SDL front end and stages a runnable copy under dist\.
rem   build.bat               the submission build (win32-scoring)
rem   build.bat <variant>     one of the variants below
rem   build.bat --variants    lists them as id;description, which the root build.bat menu reads
rem   build.bat --setup [variant]   only checks this PC can build it, offering to install what is missing
rem Every build runs that setup check first, so a missing tool is named before CMake or Gradle starts.
for %%I in ("%~dp0..\..\..") do set "ROOT=%%~fI"
cd /d "%ROOT%"

if /i "%~1"=="--variants" (
    echo win32-scoring;Windows - SDL + NVRHI, scoring profile ^(CUDA+AVX2 for the target PC, AVX2 elsewhere^), the submission build
    echo win32-nvrhi;Windows - SDL + NVRHI ^(Direct3D 12 / Vulkan^), every physics backend
    echo win32-webgpu;Windows - SDL + WebGPU ^(wgpu-native^)
    echo win32-sdlgpu;Windows - SDL + SDL_GPU
    echo android;Android APK - SDL + WebGPU ^(needs JDK 17 and the Android SDK with NDK 27.2^)
    exit /b 0
)

rem Started by double-clicking, the window would close before the result could be read. The root build.bat sets
rem BALLS_NO_PAUSE because it waits on its own.
set "WAIT_AT_END="
if not defined BALLS_NO_PAUSE echo %CMDCMDLINE% | find /i "%~f0" >nul && set "WAIT_AT_END=1"

set "SETUP_ONLY="
if /i "%~1"=="--setup" (set "SETUP_ONLY=1" & shift /1)
set "RENDERER="
set "PROFILE=all"
set "VARIANT=%~1"
if not defined VARIANT set "VARIANT=win32-scoring"
rem The submission build keeps the short dist\win32-sdl folder that the docs and scripts use.
if /i "%VARIANT%"=="win32-scoring" (set "RENDERER=nvrhi" & set "PROFILE=scoring" & set "BUILD_DIR=build\win32-scoring" & set "DIST_DIR=%ROOT%\dist\win32-sdl")
if /i "%VARIANT%"=="win32-nvrhi" (set "RENDERER=nvrhi" & set "BUILD_DIR=build\win32" & set "DIST_DIR=%ROOT%\dist\win32-sdl-all")
if /i "%VARIANT%"=="win32-webgpu" (set "RENDERER=webgpu" & set "BUILD_DIR=build\win32-webgpu" & set "DIST_DIR=%ROOT%\dist\win32-sdl-webgpu")
if /i "%VARIANT%"=="win32-sdlgpu" (set "RENDERER=sdlgpu" & set "BUILD_DIR=build\win32-sdlgpu" & set "DIST_DIR=%ROOT%\dist\win32-sdl-sdlgpu")
if /i "%VARIANT%"=="android" set "RENDERER=gradle"
if not defined RENDERER (
    echo Unknown SDL variant "%VARIANT%". Known variants:
    call "%~f0" --variants
    goto :failed
)

set "SETUP_ENV=%ROOT%\build\setup-%VARIANT%.bat"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup.ps1" -Variant %VARIANT% -EnvFile "%SETUP_ENV%"
if errorlevel 1 goto :failed
call "%SETUP_ENV%"
if defined SETUP_ONLY goto :finish
if /i "%VARIANT%"=="android" goto :android

if not exist ".deps\cuda\sdk\include\nvrtc.h" (
    echo Fetching CUDA compiler pieces for the GPU backend...
    powershell -NoProfile -ExecutionPolicy Bypass -File scripts\FetchCudaSdk.ps1
    if errorlevel 1 echo CUDA SDK fetch failed - building with CPU backends only.
)

rem Configuring every time keeps the Visual Studio solution in step with renamed projects and targets. The setup check
rem names the MSVC version to build with, since the one MSBuild defaults to can be installed without its libraries;
rem MSBuild reads it from VCToolsVersion, for CMake's compiler check as for the build.
if defined BALLS_MSVC_TOOLSET set "VCToolsVersion=%BALLS_MSVC_TOOLSET%"
if exist "%BUILD_DIR%\CMakeCache.txt" (
    cmake -S . -B "%BUILD_DIR%" -DBALLS_FRONTEND=sdl -DBALLS_RENDERER=%RENDERER% -DBALLS_PROFILE=%PROFILE% >nul
) else (
    cmake -S . -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 -DBALLS_FRONTEND=sdl -DBALLS_RENDERER=%RENDERER% -DBALLS_PROFILE=%PROFILE%
)
if errorlevel 1 goto :failed

cmake --build "%BUILD_DIR%" --config Release --parallel --target balls
if errorlevel 1 goto :failed

set "BIN_DIR=%BUILD_DIR%\bin\Release"
if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"
powershell -NoProfile -Command "Get-Process balls -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq '%DIST_DIR%\balls.exe' } | Stop-Process -Force; Start-Sleep -Milliseconds 300"
copy /y "%BIN_DIR%\balls.exe" "%DIST_DIR%\" >nul || goto :failed
if exist "%BIN_DIR%\*.dll" copy /y "%BIN_DIR%\*.dll" "%DIST_DIR%\" >nul || goto :failed
robocopy "%BIN_DIR%\assets" "%DIST_DIR%\assets" /mir /njh /njs /nfl /ndl /np >nul
if errorlevel 8 goto :failed
set "BUILT=%DIST_DIR%\balls.exe"
goto :done

:android
set "ANDROID_DIR=%~dp0android"
set "DIST_DIR=%ROOT%\dist\android-sdl"
pushd "%ANDROID_DIR%"
call "%ANDROID_DIR%\gradlew.bat" --no-daemon assembleDebug
set "GRADLE_RESULT=%ERRORLEVEL%"
popd
if not "%GRADLE_RESULT%"=="0" goto :failed
if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"
copy /y "%ANDROID_DIR%\app\build\outputs\apk\debug\app-debug.apk" "%DIST_DIR%\balls.apk" >nul || goto :failed
set "BUILT=%DIST_DIR%\balls.apk"

:done
echo.
echo Built: %BUILT%
:finish
if defined WAIT_AT_END pause
exit /b 0

:failed
echo Build failed.
if defined WAIT_AT_END pause
exit /b 1
