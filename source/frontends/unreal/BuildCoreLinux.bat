@echo off
setlocal EnableExtensions
set "REPO=%~dp0..\..\.."
set "OUT=%REPO%\build\linux-ue"
set "LOCK=%TEMP%\balls_core_linux.lock"
if not defined ANDROID_HOME set "ANDROID_HOME=%LOCALAPPDATA%\Android\Sdk"
set "NINJA=%ANDROID_HOME%\cmake\3.22.1\bin\ninja.exe"
where cmake >nul 2>&1 || (echo BuildCoreLinux: CMake not found on PATH. & exit /b 1)
if not defined LINUX_MULTIARCH_ROOT (echo BuildCoreLinux: LINUX_MULTIARCH_ROOT is not set. & exit /b 1)
if not exist "%NINJA%" (echo BuildCoreLinux: Ninja not found at "%NINJA%". & exit /b 1)

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
