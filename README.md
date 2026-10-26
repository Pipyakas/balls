# Balls

A ball-bouncing, brick-breaking color game for Windows, written in C++20.
It has an engine-agnostic core (`core/`) and an SDL3 front end (`frontends/sdl/`) that renders through SDL_GPU
(Direct3D 12, Vulkan or Metal) with Dear ImGui for text and widgets. The physics can run on three
interchangeable backends: a scalar reference, AVX2 multithreaded, and CUDA.

## Features

- **Normal mode (campaign).** Pick launchers from a 3-column queue. Each one releases its exact number of balls.
  Balls bounce with mirror reflection off walls and bricks, and break bricks of their own color. After the third
  bounce they home toward the nearest brick of their color.
- **Performance mode.** Start with one click and launchers release automatically. Spawn speed can be set to
  10 / 50 / 200 / 1000 / 2000 balls per second or Max. A ball-count multiplier (x1 to x1000) scales the stress
  test. You can switch the physics backend live with F1 (Scalar), F2 (AVX2) or F3 (CUDA).
- **Three-section screen.** The main board, the user interaction section (the only place that accepts gameplay
  input) and a debug panel. The debug panel shows FPS, min FPS, total objects, the ball count per color and the
  backend.
- **Map editor.** Paint bricks, extend them into multi-cell shapes, erase, set hit counts and colors, and edit the
  launcher grid. Auto-fill launchers to match brick hits per color, validate the level, undo/redo, and save. Then
  play the level.
- **Hot reload.** While a level is being played, saving its `.txt` files from the editor or any text editor reloads
  it within about 0.4 s.

## Build

Requirements: Windows 10/11 x64, Visual Studio 2022 (C++ workload), CMake 3.25+, Python 3 (only to fetch the CUDA
compiler pieces) and internet access on first configure (SDL 3.2.30 and Dear ImGui 1.93.0 are downloaded).

`-DBALLS_FRONTEND=none` builds only the core, the physics plugins and the tools.

```powershell
pwsh tools/FetchCudaSdk.ps1
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
build/bin/Release/ColorBlastBalls.exe
```

Or just `build.bat` / `run.bat`.

`FetchCudaSdk.ps1` downloads NVIDIA's NVRTC and runtime header wheels into `.deps/cuda`. The CUDA kernel is
compiled to PTX at build time and embedded in the exe, so the CUDA toolkit is not required. Without the SDK, or
with `-DBALLS_ENABLE_CUDA=OFF`, the game builds with CPU backends only.

At runtime only an NVIDIA driver is needed for the CUDA backend. On machines without one, the CUDA button is
disabled and the game runs on AVX2 (or Scalar on CPUs without AVX2).

### Tests and benchmark

```powershell
cmake --build build --config Release --target balls_tests balls_bench
build/bin/Release/balls_tests.exe
build/bin/Release/balls_bench.exe --balls 2000,200000,1000000 --ticks 240
build/bin/Release/balls_bench.exe --full --balls 100000
build/bin/Release/balls_bench.exe --roofline --balls 4000000,40000000
python tools/bench/probe_bench.py --backends cuda avx2 --fps 60
```

### Shaders

The SDL front end ships its shaders precompiled in `frontends/sdl/shaders/generated/ShaderBlobs.h`, so builds
need no shader compiler. After editing an `.hlsl` file, regenerate it with DXC
(https://github.com/microsoft/DirectXShaderCompiler/releases, unpacked to `.deps/dxc`):

```powershell
python tools/shaders/CompileShaders.py
```

## Physics plugins

Physics backends are DLL plugins loaded from next to the executable, so physics work only
rebuilds its plugin, not the core or a front end:

```powershell
cmake --build build --config Release --target balls_physics_cuda
```

Every time a backend is selected the game loads a fresh shadow copy of its DLL from
`%TEMP%\ColorBlastBalls\physics-plugins`, so a plugin can be rebuilt while the game runs and
picked up with F1/F2/F3 (or `/backend?kind=cuda` on the control API). Plugins only see
`core/sim/PhysicsBackend.h` and `core/sim/kernels/BallStepCore.h`. The build uses the shared MSVC
runtime and copies its DLLs next to the executable.

## Controls

| Where | Input | Action |
|---|---|---|
| Normal mode | Left-click a launcher | Release its balls |
| Performance mode | Start button / Space | Begin automatic release |
| Any game | Esc / P | Pause menu |
| Any game | R | Restart level |
| Any game | F1 / F2 / F3 | Scalar / AVX2 / CUDA physics |
| Editor | L-drag / R-drag | Apply tool / erase |
| Editor | 1-7, P/E/X/K | Color, tool |
| Editor | Ctrl+Z / Ctrl+Y / Ctrl+S | Undo / redo / save |

## Level format

Each level is a pair of files in `assets/levels/normal` or `assets/levels/performance`, in the format these sample levels use:

- `<Name>_Bricks.txt`
  - `[Board] Width / Height / PlayableHeight`: brick rows, plus total rows including the empty play space.
  - `[Layout]`: a grid of brick ids. Cells that share an id form one multi-cell brick. `-1` marks an empty cell.
  - `[BrickTypes]`: one `id Color hits` line per brick.
- `<Name>_SpawnBall.txt`
  - `[Board] Width` (launcher columns) and `Height` (visible rows).
  - `[Layout]`: `Color(count)` or `Empty` per slot.

Colors: Red, Orange, Yellow, Green, Blue, Indigo, Violet.

## Project layout

```
core/                 engine-agnostic library (no SDL, C++20, no exceptions)
  common/             BallColor, Result<T>, DynamicLibrary
  level/              parse / write / watch level files
  sim/                BrickBoard, TargetField, Simulation, physics backends, CUDA kernels
  game/               GameSession, LauncherQueue, SpawnController
  editor/             LevelEditor model used by any front end
physics/cpu/          balls_physics_cpu.dll plugin: Scalar and AVX2 backends
physics/cuda/         balls_physics_cuda.dll plugin: CUDA backend and kernels
frontends/sdl/        SDL3 + SDL_GPU + Dear ImGui front end: screens, map editor UI, control API
  render/             IRenderer, the SDL_GPU renderer (compute ball culling, impostor balls, instanced rects)
  shaders/            HLSL sources; generated/ShaderBlobs.h holds SPIR-V and DXIL built by tools/shaders
tools/                bench, NVRTC PTX generator, CUDA SDK fetch script, shader compiler script
tests/                unit tests (balls_tests)
assets/levels/        level files
```

## Git workflow

- `main` holds tagged releases.
- `develop` is the integration branch.
- Work happens on `feature/*` branches, which are merged into `develop` with `--no-ff`.
- Commits use `feat:` / `fix:` / `perf:` / `refactor:` / `build:` / `chore:` prefixes.
