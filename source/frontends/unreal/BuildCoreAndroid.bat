@echo off
rem Builds balls_core for Android arm64 with the NDK, and stages the level assets for APK packaging.
setlocal EnableExtensions
set "REPO=%~dp0..\..\.."
set "OUT=%REPO%\build\android-ue"
set "LOCK=%TEMP%\balls_core_android.lock"
if not defined ANDROID_HOME set "ANDROID_HOME=%LOCALAPPDATA%\Android\Sdk"
if not defined NDK_ROOT set "NDK_ROOT=%ANDROID_HOME%\ndk\27.2.12479018"
rem The SDK's CMake is 3.22, but balls_core needs 3.25+; use the machine's CMake with the SDK's Ninja.
where cmake >nul 2>&1 || (echo BuildCoreAndroid: CMake not found on PATH. & exit /b 1)
set "NINJA=%ANDROID_HOME%\cmake\3.22.1\bin\ninja.exe"

if not exist "%NINJA%" (echo BuildCoreAndroid: Ninja not found at "%NINJA%". & exit /b 1)
if not exist "%NDK_ROOT%\build\cmake\android.toolchain.cmake" (echo BuildCoreAndroid: NDK not found at "%NDK_ROOT%". & exit /b 1)

rem UnrealBuildTool runs this pre-build step once per target and builds targets in parallel, so several
rem copies can reach the same Ninja tree at once. Serialize them with a lock directory; a lock older than
rem 15 minutes is treated as left by a crash.
:acquire
2>nul mkdir "%LOCK%"
if not errorlevel 1 goto locked
powershell -NoProfile -Command "if ((Test-Path -LiteralPath '%LOCK%') -and ((Get-Date) - (Get-Item -LiteralPath '%LOCK%').LastWriteTime).TotalMinutes -gt 15) { Remove-Item -LiteralPath '%LOCK%' -Recurse -Force }" >nul 2>&1
ping 127.0.0.1 -n 3 >nul
goto acquire

:locked
if not exist "%OUT%\CMakeCache.txt" (
    cmake -S "%REPO%" -B "%OUT%" -G Ninja -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
        -DCMAKE_TOOLCHAIN_FILE="%NDK_ROOT%\build\cmake\android.toolchain.cmake" ^
        -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DANDROID_STL=c++_static ^
        -DCMAKE_BUILD_TYPE=Release -DBALLS_BUILD_GAME=OFF -DBALLS_PROFILE=extended -DBALLS_ENABLE_CUDA=OFF -DBALLS_LTO=OFF
    if errorlevel 1 goto failed
)
cmake --build "%OUT%" --target balls_core --parallel
if errorlevel 1 (
    echo BuildCoreAndroid: balls_core failed to build.
    goto failed
)

rem DirectoriesToAlwaysStageAsNonUFS paths are relative to the game Content directory, so the levels
rem must live in source/frontends/unreal/Content/assets for the package to carry them.
if not exist "%~dp0Content\assets" mkdir "%~dp0Content\assets"
xcopy "%REPO%\data\assets\*" "%~dp0Content\assets\" /E /I /Y >nul
if errorlevel 1 (
    echo BuildCoreAndroid: could not stage levels into "%~dp0Content\assets".
    goto failed
)
rmdir "%LOCK%" 2>nul
exit /b 0

:failed
rmdir "%LOCK%" 2>nul
exit /b 1
