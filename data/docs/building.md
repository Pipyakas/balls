# Building and testing

The [README](../../README.md) has the short version; this page has every option.

## Windows

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
| `build.bat unreal win64`, `build.bat unreal linux` | `dist\win32-ue\ColorBlastBalls.exe`, `dist\linux-ue\` (needs Unreal Engine 5.8 at `UE_ROOT`; a stock install works) |
| `build.bat unity win64` | `dist\win32-unity\ColorBlastBallsUnity.exe` (Mono player; needs the Unity version in `source\frontends\unity\ProjectSettings\ProjectVersion.txt`, or `UNITY_EXE`) |
| `build.bat unity linux` | `dist\linux-unity\ColorBlastBallsUnity.x86_64` (IL2CPP; needs the Linux `.so` files, built on Linux, in `source\frontends\unity\NativeLibraries\linux-x86_64`) |

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

## Linux and macOS

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

## Tests and benchmark

After `build.bat`, the unit tests and the benchmark build and run from the same folder:

```powershell
cmake --build build/win32-scoring --config Release --target balls_tests balls_bench
build/win32-scoring/bin/Release/balls_tests.exe
build/win32-scoring/bin/Release/balls_bench.exe --balls 2000,200000,1000000 --ticks 240
build/win32-scoring/bin/Release/balls_bench.exe --fill --balls 262144 --ticks 100
```

## Shaders

The SDL front end ships its shaders precompiled in `source/frontends/sdl/shaders/generated/ShaderBlobs.h`, so builds
need no shader compiler. After editing an `.hlsl` file, regenerate it with DXC
(https://github.com/microsoft/DirectXShaderCompiler/releases, unpacked to `.deps/dxc`):

```powershell
python scripts/CompileShaders.py
```
