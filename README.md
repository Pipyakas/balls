# Balls

A ball-bouncing, brick-breaking color game in C++20: one engine-agnostic core, four front ends (SDL, Unreal Engine,
Unity, WebAssembly) and interchangeable physics backends (Scalar, AVX2, Zen4, CUDA, ROCm, Vulkan).

## Build and run

Windows 10/11 x64, Visual Studio 2022 with the C++ workload, CMake 3.25+, Python 3. Double-click `build.bat`, then run
`dist\win32-sdl\balls.exe`.

Linux, CMake 3.25+ and a C++20 compiler: `./build.sh`, then `build/linux/bin/balls`.

Browser: Emscripten on `PATH`, then
`emcmake cmake -S . -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build-web` and serve `build-web`
with any static server; open `balls.html`.

Android: `build.bat sdl android`, with JDK 17 and an Android SDK with NDK 27.2.

## Play

Performance mode, pick a level and press Start. Spawn / sec sets the release rate; Ball limit `Level` releases the
level's launcher counts, `Auto` keeps releasing until memory is full, `Max` carries on into the page file or swap.
Normal mode releases a launcher's balls when you click it.
