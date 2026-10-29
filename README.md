# Balls

A ball-bouncing, brick-breaking color game for Windows, written in C++20.
It has an engine-agnostic core (`source/core/`) and an SDL3 front end (`source/frontends/sdl/`) with Dear ImGui for text and
widgets. Rendering sits behind `IRenderer`: NVRHI (Direct3D 12 or Vulkan) on Windows, Linux and macOS (via
MoltenVK), and SDL_GPU for Android and other SDL targets. The physics can run on
interchangeable backends: a scalar reference, AVX2 and AVX-512 (Zen4) multithreaded, CUDA, ROCm (AMD GPUs through
the HSA runtime, Linux), Vulkan compute, VulkanMulti (one Vulkan backend across several GPUs), CUDA+AVX2, a fixed
split of the balls between those two, and Hybrid, which runs one backend on each device the player picks (the CPU
and every GPU) and splits the balls between all of them (see
[data/docs/hybrid-physics.md](data/docs/hybrid-physics.md)).

## Quick start

On Windows 10/11 with Visual Studio 2022 (C++ workload), CMake 3.25+ and Python 3:

1. Double-click `build.bat` (or run it from a terminal). It checks the PC first and names anything missing, then
   builds the submission version into `dist\win32-sdl\balls.exe`. The first build downloads its dependencies.
2. Run `dist\win32-sdl\balls.exe`. Normal mode has the campaign levels; Performance mode has the samples'
   `DA_1000Bricks` and our `Unlimited` level, whose launchers never run out.
3. For the most balls in Performance mode: pick `Unlimited`, set Spawn / sec to `Max` and Ball count to `x10000`,
   leave Ball limit on `Auto`, then `Start`. Auto is everything the physics can hold, measured when the game starts,
   and balls are only added as fast as every physics frame stays within 1/60 s. Close other programs first,
   GPU-heavy ones especially (see the scoring profile below).
4. Tests: see [Tests and benchmark](#tests-and-benchmark).

The submission is tagged (`git tag -l`); each tag is a build that passed the tests and was played on the target PC.

## Features

- **Normal mode (campaign).** Pick launchers from a 3-column queue. Each one releases its exact number of balls.
  Balls bounce with mirror reflection off walls and bricks, and break bricks of their own color. After the third
  bounce they home toward the nearest brick of their color.
- **Performance mode.** Start with one click and launchers release automatically. Spawn speed can be set to
  10 / 50 / 200 / 1000 / 2000 balls per second or faster up to Max, and seven launchers release at once so every
  color is on the board together. A ball-count multiplier (x1 to x10000) scales the stress test. In the extended
  build you can switch the physics backend live from the control gadget's Advanced Settings, and pick one
  backend per device for Hybrid (for example AVX2 on the CPU, CUDA on an RTX card and Vulkan on a Radeon).
- **Effects.** Bricks flash when hit, slide up into gaps and burst into pieces when broken; a ball that hits its
  brick leaves a ring in its color and shrinks away.
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

`-DBALLS_FRONTEND=none` builds only the core, the physics plugins and the tools. `-DBALLS_RENDERER=nvrhi|webgpu|sdlgpu`
picks the renderer (default: `nvrhi` on desktop, `webgpu` on Android). With NVRHI, every platform runs Vulkan by default;
`--api d3d12` switches Windows to Direct3D 12.

`-DBALLS_PROFILE=scoring` builds for the target PC only (i9-14900K, GTX 1650): AVX2 code throughout, sm_75
CUDA kernels, and the CUDA+AVX2 pair as the one physics backend, with no backend menu or machine benchmark. The pair
allocates its whole memory budget once per process (free VRAM less 512 MB, available RAM less the usual margin), so
balls never grow, copy or page-fault during play. Auto is that capacity, with no probe, and ramping up to it keeps every physics frame within 1/60 s. The default, `all`, is the extended build with every backend; set
`BALLS_PREALLOCATE=1` there to try the same up-front allocation on the pair.
To try the scoring build on another NVIDIA GPU, set `-DBALLS_SCORING_CUDA_SM` to its architecture (86 for an RTX 30
series); the default, 75, is the target PC's GTX 1650.

To get the most balls out of the scoring build, close other programs before starting it, GPU-heavy ones especially.
The budget is measured once, when the game starts: every GB of RAM or VRAM that is free at that moment holds about
67 million more balls (16 bytes a ball), and anything opened later doesn't change it. On the 64 GB target PC, Auto
measured between 7.07 and 7.29 billion balls depending on what else was running.

`build.bat` (Windows) and `build.sh` (Linux) at the project root are the one entry point for every front end and
platform:

| Command | Builds |
|---|---|
| `build.bat`, or double-click it | the submission version, SDL + NVRHI with the scoring profile, into `dist\win32-sdl\balls.exe` |
| `build.bat --menu` | a numbered list of every front end and platform to pick from |
| `build.bat sdl <variant>` | one variant directly, e.g. `build.bat sdl android` |
| `build.bat --setup [--menu \| sdl <variant>]` | only the setup check below, without building |
| `./build.sh [--menu \| sdl <variant>]` | the same on Linux; the default is SDL + NVRHI (Vulkan) into `dist/linux-sdl/balls` |

Each front end owns its build in `source/frontends/<name>/build.bat` and `build.sh`, which can also be run on their
own. The root scripts only dispatch to them, and the menu lists whatever each one reports with `--variants`, so a
new front end adds its scripts to its own folder and appears in the menu. The SDL front end's variants:

| Variant | Output |
|---|---|
| `win32-scoring` (default on Windows): the scoring profile, CUDA+AVX2 on the target PC and AVX2 on any other | `dist\win32-sdl\balls.exe` |
| `win32-nvrhi`: the extended build, every physics backend | `dist\win32-sdl-all\balls.exe` |
| `win32-webgpu`, `win32-sdlgpu` | `dist\win32-sdl-webgpu\`, `dist\win32-sdl-sdlgpu\` |
| `linux-nvrhi` (default on Linux), `linux-webgpu`, `linux-sdlgpu` | `dist/linux-sdl/`, `dist/linux-sdl-webgpu/`, `dist/linux-sdl-sdlgpu/` |
| `android` | `dist\android-sdl\balls.apk` (needs JDK 17, and `ANDROID_HOME` pointing at an SDK with NDK 27.2) |

The engine front ends follow the same pattern from Windows:

| Command | Output |
|---|---|
| `build.bat unreal win64`, `build.bat unreal linux` | `dist\win32-ue\ColorBlastBalls.exe`, `dist\linux-ue\` (needs the engine fork at `UE_ROOT`) |
| `build.bat unity win64` | `dist\win32-unity\ColorBlastBallsUnity.exe` (Mono player; needs the Unity version in `frontends\unity\ProjectSettings\ProjectVersion.txt`, or `UNITY_EXE`) |
| `build.bat unity linux` | `dist\linux-unity\ColorBlastBallsUnity.x86_64` (IL2CPP; needs the Linux `.so` files, built on Linux, in `frontends\unity\NativeLibraries\linux-x86_64`) |

Every build starts with a setup check (`source/frontends/<name>/setup.ps1`, `setup.sh` on Linux) that lists each
thing the chosen variant needs with `ok` or `MISS`: for Windows desktop builds Visual Studio 2022 with the C++
tools, a Windows SDK, CMake 3.25+, Git, Python (only to fetch the CUDA compiler pieces) and internet access for the
first configure; for Android JDK 17+, the Android SDK with NDK 27.2, platform 35 and an SDK CMake 3.25+. It offers
to install what is missing (winget on Windows, apt or dnf on Linux, `sdkmanager` for Android SDK packages), finds
tools that are installed but not on `PATH` (such as the CMake bundled with Visual Studio), and stops before CMake or
Gradle starts when something required is still missing.

Every build keeps CMake's intermediate files under `build\`, stages a self-contained runnable copy under `dist\`,
stops a game still running from that copy first, and ends by printing the absolute path of what it built.
Started by double-click, the Windows scripts wait for a key before closing.

The tests, benchmarks and tools below use the Windows CMake tree directly:

```powershell
pwsh scripts/FetchCudaSdk.ps1
cmake -S . -B build/win32 -G "Visual Studio 17 2022" -A x64
cmake --build build/win32 --config Release --parallel
build/win32/bin/Release/balls.exe
```

### Linux and macOS

Requirements: a C++20 compiler (GCC 12+ or Clang 15+), CMake 3.25+, Ninja or Make, and internet access on first
configure (SDL 3.2.30, Dear ImGui 1.93.0 and NVRHI with the Vulkan headers are downloaded). Vulkan drivers are
needed at runtime on Linux; macOS uses MoltenVK. The CUDA backend is available on Windows and Linux (via
scripts/FetchCudaSdk.sh); macOS has no CUDA backend.

```sh
./build.sh                                        # SDL + NVRHI into dist/linux-sdl
dist/linux-sdl/balls                              # run the staged copy
dist/linux-sdl/balls --play normal DA_49Bricks --backend avx2
```

Tests and the benchmark build in the same CMake tree: `cmake --build build/linux --target balls_tests balls_bench`.

The ROCm backend (AMD GPUs, Linux only) needs the ROCm runtime (`libhsa-runtime64.so.1`) and ROCm's clang with its
device libraries at build time; `scripts/FetchRocmSdk.sh` downloads the HSA headers into `.deps/rocm`. Its kernels
are OpenCL C compiled to gfx1030/gfx1031 code objects embedded in the plugin; no HIP is used. Without the headers
or the compiler the plugin is skipped.

To start it from the application menu or the desktop, run `scripts/install-desktop-shortcut.sh` once; it writes a
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

After `build.bat`, the unit tests and the benchmark build and run from the same folder:

```powershell
cmake --build build/win32-scoring --config Release --target balls_tests balls_bench
build/win32-scoring/bin/Release/balls_tests.exe
build/win32-scoring/bin/Release/balls_bench.exe --balls 2000,200000,1000000 --ticks 240
build/win32-scoring/bin/Release/balls_bench.exe --fill --balls 262144 --ticks 100
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
`%TEMP%\balls\physics-plugins`, so a plugin can be rebuilt while the game runs and
picked up by selecting the backend again (or `/backend?kind=cuda` on the control API). Plugins only see
`source/core/sim/PhysicsBackend.h` and `source/core/sim/kernels/BallStepCore.h`. The build uses the shared MSVC
runtime and copies its DLLs next to the executable.

### Devices and Hybrid

Each plugin lists the devices it can run on, and the game merges them into physical devices: `cpu`, then
`gpu0`, `gpu1`, ... ordered by PCI location, with a GPU that several plugins see listed once (matched by UUID, or
by PCI location for ROCm). `balls_bench --devices` prints them. A selection gives each device at most one backend;
VulkanMulti claims several devices, joined with `|`:

```powershell
balls.exe --backend "AVX2@cpu+CUDA@gpu0+Vulkan@gpu1"   # Hybrid over three devices
balls.exe --backend "vulkan@6700"                       # one backend pinned to a device (name substring)
balls --backend "Zen4@cpu+CUDA@gpu1+ROCm@gpu0"          # AVX-512 CPU, CUDA and ROCm
balls --backend "VulkanMulti@gpu0|gpu1"                 # one Vulkan backend over two GPUs
balls_bench.exe --grid --balls 32000000 --backend "cuda,cuda+avx2,cuda@gpu0+vulkan@gpu1"
```

The same text works in `/backend?kind=` and is what Settings saves. `--backend hybrid` composes the best backend
on the CPU and every discrete GPU.

## Controls

| Where | Input | Action |
|---|---|---|
| Normal mode | Left-click a launcher | Release its balls |
| Performance mode | Start button / Space | Begin automatic release |
| Any game | Esc / P | Pause menu |
| Any game | R | Restart level |
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

`performance/Unlimited` has the `DA_1000Bricks` board with one launcher per color at the largest count the format
takes (2,147,483,647), so even at x1 it holds more balls than any machine's capacity: auto release keeps spawning
until the ball limit stops it.

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
  backends/cpu/       balls_physics_cpu.dll plugin: Scalar, AVX2 and Zen4 (AVX-512) backends
  backends/cuda/      balls_physics_cuda.dll plugin: CUDA backend and kernels
  backends/rocm/      libballs_physics_rocm.so plugin (Linux): ROCm backend on HSA, OpenCL C kernels
  backends/vulkan/    balls_physics_vulkan.dll plugin: Vulkan and VulkanMulti backends
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
- Commit small, working steps and push after every commit, so other machines never wait on unpushed work.
