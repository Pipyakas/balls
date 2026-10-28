@echo off
rem Cross-compiles balls_core and the physics plugins for Linux x86_64 with Unreal's own Linux toolchain
rem (LINUX_MULTIARCH_ROOT), into build\linux-ue. The CUDA plugin is Windows-only; the CPU and Vulkan plugins build.
setlocal EnableExtensions
set "REPO=%~dp0..\..\.."
set "OUT=%REPO%\build\linux-ue"
set "LOCK=%TEMP%\balls_core_linux.lock"
if not defined ANDROID_HOME set "ANDROID_HOME=%LOCALAPPDATA%\Android\Sdk"
set "NINJA=%ANDROID_HOME%\cmake\3.22.1\bin\ninja.exe"
where cmake >nul 2>&1 || (echo BuildCoreLinux: CMake not found on PATH. & exit /b 1)
if not defined LINUX_MULTIARCH_ROOT (echo BuildCoreLinux: LINUX_MULTIARCH_ROOT is not set. & exit /b 1)
if not exist "%NINJA%" (echo BuildCoreLinux: Ninja not found at "%NINJA%". & exit /b 1)

rem UnrealBuildTool runs this pre-build step once per target, so serialize copies with a lock directory; a lock
rem older than 15 minutes is treated as left by a crash.
:acquire
2>nul mkdir "%LOCK%"
if not errorlevel 1 goto locked
powershell -NoProfile -Command "if ((Test-Path -LiteralPath '%LOCK%') -and ((Get-Date) - (Get-Item -LiteralPath '%LOCK%').LastWriteTime).TotalMinutes -gt 15) { Remove-Item -LiteralPath '%LOCK%' -Recurse -Force }" >nul 2>&1
ping 127.0.0.1 -n 3 >nul
goto acquire

:locked
if not exist "%OUT%\CMakeCache.txt" (
    cmake -S "%REPO%" -B "%OUT%" -G Ninja -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
        -DCMAKE_TOOLCHAIN_FILE="%~dp0LinuxToolchain.cmake" -DCMAKE_BUILD_TYPE=Release ^
        -DBALLS_FRONTEND=none -DBALLS_ENABLE_CUDA=OFF -DBALLS_LTO=OFF -DBALLS_BUILD_TOOLS=OFF
    if errorlevel 1 goto failed
)
cmake --build "%OUT%" --target balls_core balls_physics_plugins --parallel
if errorlevel 1 (
    echo BuildCoreLinux: balls_core failed to build.
    goto failed
)
rmdir "%LOCK%" 2>nul
exit /b 0

:failed
rmdir "%LOCK%" 2>nul
exit /b 1
