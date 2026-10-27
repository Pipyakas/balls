# Balls

A ball-bouncing, brick-breaking color game for Windows, written in C++20.
It has an engine-agnostic core (`source/core/`) and an SDL3 front end (`source/frontends/sdl/`) with Dear ImGui for text and
widgets. Rendering sits behind `IRenderer`: NVRHI (Direct3D 12 or Vulkan) on Windows, Linux and macOS (via
MoltenVK), and SDL_GPU for Android and other SDL targets. The physics can run on four
interchangeable backends: a scalar reference, AVX2 multithreaded, CUDA, and a hybrid that splits the balls
between CUDA and AVX2 (see [data/docs/hybrid-physics.md](data/docs/hybrid-physics.md)).

## Features

- **Normal mode (campaign).** Pick launchers from a 3-column queue. Each one releases its exact number of balls.
  Balls bounce with mirror reflection off walls and bricks, and break bricks of their own color. After the third
  bounce they home toward the nearest brick of their color.
- **Performance mode.** Start with one click and launchers release automatically. Spawn speed can be set to
  10 / 50 / 200 / 1000 / 2000 balls per second or Max. A ball-count multiplier (x1 to x1000) scales the stress
  test. You can switch the physics backend live with F1 (Scalar), F2 (AVX2), F3 (CUDA) or F4 (Hybrid).
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
compiler pieces) and internet access on first configure (SDL 3.2.30, Dear ImGui 1.93.0 and NVRHI with the Vulkan and DirectX headers are downloaded).

`-DBALLS_FRONTEND=none` builds only the core, the physics plugins and the tools. `-DBALLS_RENDERER=nvrhi|sdlgpu`
picks the renderer (default: `nvrhi`, or `sdlgpu` on Android). With NVRHI, Windows runs Direct3D 12 and
`--api vulkan` switches to Vulkan; Linux and macOS always use Vulkan.

Every front end has one build and one run script per platform at the project root, named
`<build|run>-<platform>-<frontend>`. Each one stages a self-contained runnable copy in `dist\<platform>-<frontend>\`
and keeps CMake's intermediate files in `build\<platform>\`. Building stops a game instance still running from
that `dist` folder first.

| Script | Output |
|---|---|
| `build-win32-sdl.bat` / `run-win32-sdl.bat [game args]` | `dist\win32-sdl\ColorBlastBalls.exe` |
| `build-linux-sdl.sh` / `run-linux-sdl.sh [game args]` | `dist/linux-sdl/ColorBlastBalls` |

The UE front end and the Android package are built on their own machines; they add
`build-win32-ue.bat` / `build-android-sdl.sh` in the same pattern, writing to `dist\win32-ue\` and
`dist/android-sdl/`.

The tests, benchmarks and tools below use the Windows CMake tree directly:

```powershell
pwsh scripts/FetchCudaSdk.ps1
cmake -S . -B build/win32 -G "Visual Studio 17 2022" -A x64
cmake --build build/win32 --config Release --parallel
build/win32/bin/Release/ColorBlastBalls.exe
```

### Linux and macOS

Requirements: a C++20 compiler (GCC 12+ or Clang 15+), CMake 3.25+, Ninja or Make, and internet access on first
configure (SDL 3.2.30, Dear ImGui 1.93.0 and NVRHI with the Vulkan headers are downloaded). Vulkan drivers are
needed at runtime on Linux; macOS uses MoltenVK. There is no CUDA backend off Windows.

```sh
./build.sh                     # Release, NVRHI renderer, no CUDA
./build.sh --debug -j 8        # Debug build with all CPUs
./build.sh --renderer sdlgpu   # SDL_GPU instead of NVRHI
./run.sh                       # run from build/bin
./run.sh --play normal DA_49Bricks --backend avx2
```

`./build.sh --clean` reconfigures from scratch, and anything after `--` is passed to CMake. Tests and the
benchmark build next to the game: `build/bin/balls_tests`, `build/bin/balls_bench`.

To start it from the application menu or the desktop, run `tools/install-desktop-shortcut.sh` once; it writes a
`.desktop` entry with this checkout's absolute paths.

`FetchCudaSdk.ps1` downloads NVIDIA's NVRTC and runtime header wheels into `.deps/cuda`. The CUDA kernel is
compiled at build time to SASS for sm_50, 60, 70, 75, 80, 86 and 89 plus `compute_75` and `compute_50` PTX, and
all of it is embedded in the CUDA plugin, so the CUDA toolkit is not required. Without the SDK, or with
`-DBALLS_ENABLE_CUDA=OFF`, the game builds with CPU backends only.

At runtime only an NVIDIA driver is needed for the CUDA backend: R527 or newer for the SASS images, which
cover Maxwell through Ada, or one that supports CUDA 12.9 for the PTX that newer GPUs JIT. Set
`BALLS_CUDA_IMAGE=sm_75` (or `compute_50`, ...) to force one image. On machines without one, the CUDA button is
disabled and the game runs on AVX2 (or Scalar on CPUs without AVX2).

### Tests and benchmark

```powershell
cmake --build build/win32 --config Release --target balls_tests balls_bench
build/win32/bin/Release/balls_tests.exe
build/win32/bin/Release/balls_bench.exe --balls 2000,200000,1000000 --ticks 240
build/win32/bin/Release/balls_bench.exe --full --balls 100000
build/win32/bin/Release/balls_bench.exe --roofline --balls 4000000,40000000
python scripts/bench/probe_bench.py --backends cuda avx2 --fps 60
```

### Shaders

The SDL front end ships its shaders precompiled in `source/frontends/sdl/shaders/generated/ShaderBlobs.h`, so builds
need no shader compiler. After editing an `.hlsl` file, regenerate it with DXC
(https://github.com/microsoft/DirectXShaderCompiler/releases, unpacked to `.deps/dxc`):

```powershell
python scripts/CompileShaders.py
```

### CUDA limits

`data/docs/cuda-limits.md` shows the CUDA step kernel at the GTX 1650's limits: DRAM bandwidth on an empty board (93.7% of
copy bandwidth), and on the brick level DRAM bandwidth in isolation and the 82 W board power limit under
sustained load.

### Compute shaders instead of CUDA

`data/docs/gpu-compute-apis.md` ports the step kernel and cell sort to HLSL and runs them through D3D11, D3D12 and
Vulkan (`source/tools/gpucompute/`). Vulkan comes within 1.5% of CUDA on the brick level and 6% on the empty board.

`data/docs/hardware-support.md` lists the oldest GPUs, CPUs, drivers and OS versions each backend and renderer API
supports.

## Physics plugins

Physics backends are DLL plugins loaded from next to the executable, so physics work only
rebuilds its plugin, not the core or a front end:

```powershell
cmake --build build/win32 --config Release --target balls_physics_cuda
```

Every time a backend is selected the game loads a fresh shadow copy of its DLL from
`%TEMP%\ColorBlastBalls\physics-plugins`, so a plugin can be rebuilt while the game runs and
picked up with F1-F4 (or `/backend?kind=cuda` on the control API). Plugins only see
`source/core/sim/PhysicsBackend.h` and `source/core/sim/kernels/BallStepCore.h`. The build uses the shared MSVC
runtime and copies its DLLs next to the executable.

## Controls

| Where | Input | Action |
|---|---|---|
| Normal mode | Left-click a launcher | Release its balls |
| Performance mode | Start button / Space | Begin automatic release |
| Any game | Esc / P | Pause menu |
| Any game | R | Restart level |
| Any game | F1 / F2 / F3 / F4 | Scalar / AVX2 / CUDA / Hybrid physics |
| Editor | L-drag / R-drag | Apply tool / erase |
| Editor | 1-7, P/E/X/K | Color, tool |
| Editor | Ctrl+Z / Ctrl+Y / Ctrl+S | Undo / redo / save |

## Level format

Each level is a pair of files in `data/assets/levels/normal` or `data/assets/levels/performance`, in the format these sample levels use:

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
data/
  assets/             fonts and level files (copied next to the executable at build time)
  docs/               design notes and measurements
source/
  core/               engine-agnostic library (no SDL, C++20, no exceptions)
    common/           BallColor, Result<T>, DynamicLibrary
    level/            parse / write / watch level files
    sim/              BrickBoard, TargetField, Simulation, physics backend interface, CUDA kernel core
    game/             GameSession, LauncherQueue, SpawnController
    editor/           LevelEditor model used by any front end
  backends/cpu/       balls_physics_cpu.dll plugin: Scalar and AVX2 backends
  backends/cuda/      balls_physics_cuda.dll plugin: CUDA backend and kernels
  frontends/sdl/      SDL3 + SDL_GPU + Dear ImGui front end: screens, map editor UI, control API
    render/           IRenderer, the SDL_GPU renderer (compute ball culling, impostor balls, instanced rects)
    render/nvrhi/     NVRHI renderer: D3D12 and Vulkan devices and swapchains, ImGui backend, GPU timer queries
    shaders/          HLSL sources; generated/ShaderBlobs.h holds SPIR-V and DXIL built by scripts/CompileShaders.py
  tests/              unit tests (balls_tests)
  tools/              benchmarks, instruction counter, NVRTC PTX generator, compute-shader port
scripts/              CUDA SDK fetch, shader compilers, benchmark drivers, remote benchmark machine setup
dist/                 build output, one runnable folder per <platform>-<frontend> (not committed)
build-*/run-*         build and launch one front end on one platform
CMakeLists.txt        builds everything under source/
```

## Git workflow

- `main` is the only trunk: finished, building changes land on it directly or by a `--no-ff` merge of a
  `feature/*` branch.
- `feature/*` branches hold side work that is not ready for `main`, and they merge straight into `main`. There is
  no `develop` or any other integration branch: one trunk, short-lived feature branches.
- `submission` is generated from `main` for the game's scoring repository.
- Commits use `feat:` / `fix:` / `perf:` / `refactor:` / `build:` / `docs:` / `chore:` prefixes.
- Commit small, working steps and push after every commit, so other machines and agents never wait on unpushed work.
