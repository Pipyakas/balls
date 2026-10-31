# Balls

A ball-bouncing, brick-breaking color game for Windows, written in C++20. An
engine-agnostic core (`source/core/`) runs the game; the SDL3 front end (`source/frontends/sdl/`) draws it through
NVRHI (Vulkan or Direct3D 12) with Dear ImGui for the UI. The physics runs on interchangeable backends, from a
scalar reference to AVX2, CUDA and a split of the balls across every device: see
[physics backends](data/docs/physics-backends.md).

**The SDL front end is the main version.** `build.bat` builds it, and it is the only front end tuned for the
target PC ([scoring build](data/docs/scoring-pc.md)). The Unreal Engine and Unity front ends are tech demos: they
run the same core (Unity also has its own Burst physics backend) to show it inside each engine, but each engine
adds its own per-frame work and copies the balls through its own renderer, so they may not show the best
performance the core can reach.

## Download

The latest build is on the [releases page](https://github.com/Pipyakas/balls/releases/latest)
(Windows 10/11 x64). Each release has three zips:

| Zip | Run | |
|---|---|---|
| `balls-<version>-win64-sdl.zip` | `win32-sdl\balls.exe` | The main build |
| `balls-<version>-win64-unity.zip` | `win32-unity\ColorBlastBallsUnity.exe` | Unity tech demo |
| `balls-<version>-win64-unreal.zip` | `win32-ue\Balls.exe` | Unreal Engine tech demo |

Unzip and run. An NVIDIA GPU adds the CUDA physics backend, and the CPU backends run anywhere.

For the most balls: Performance mode, level `Unlimited_921Bricks`, then `Start`. Spawn / sec `Max` is the default,
and the level's seven launchers hold 2,147,483,647 balls each, more than the machine holds (or pick Ball limit
`Auto`, which keeps any level releasing until the machine is full). Close other programs first: Auto is the RAM and
VRAM free when the game starts.

## Build

Requirements: Windows 10/11 x64, Visual Studio 2022 (C++ workload), CMake 3.25+, Python 3, and internet access on
the first build.

1. Double-click `build.bat`. It checks the PC, names anything missing, and builds the main SDL build into
   `dist\win32-sdl\balls.exe`.
2. Run `dist\win32-sdl\balls.exe`.

The tech demos build from the same script, each needing its engine installed:

| Command | Output | Needs |
|---|---|---|
| `build.bat unity win64` | `dist\win32-unity\ColorBlastBallsUnity.exe` | The Unity version in `source\frontends\unity\ProjectSettings\ProjectVersion.txt` (or `UNITY_EXE`) |
| `build.bat unreal win64` | `dist\win32-ue\Balls.exe` | Unreal Engine 5.8.4 (found by itself, or `UE_ROOT`) |

Linux, Android and every build option: [building and testing](data/docs/building.md). Each release is tagged
(`git tag -l`).

## Tests

After `build.bat`:

```powershell
cmake --build build/win32-scoring --config Release --target balls_tests balls_bench
build/win32-scoring/bin/Release/balls_tests.exe
build/win32-scoring/bin/Release/balls_bench.exe --balls 2000,200000,1000000 --ticks 240
```

The whole verification pass, logic and UI, runs from one script: it builds and runs `balls_tests` and the physics
accuracy check, then drives each built front end through its control API (every screen, each Normal level played to
the win, each Performance level filled, a broken and a repaired map, a clean quit) and writes
`build/verify-report/report.html` with a screenshot per check.

```powershell
python -I scripts/test/RunTests.py                  # logic, SDL and Unreal
python -I scripts/test/RunTests.py --stages sdl --quick
```

Other front ends (Unreal, Unity), Linux, Android, build options and the benchmark flags:
[building and testing](data/docs/building.md). The scoring build's design and PC measurements:
[target PC](data/docs/scoring-pc.md).

## Features

- **Normal mode.** Pick launchers from a 3-column queue; each releases its exact number of balls. Balls bounce off
  walls and bricks, break bricks of their own color, and home toward the nearest one after the third bounce.
- **Performance mode.** One click starts automatic release from seven launchers at once, at 10 to 2000 balls per
  second (the game's options) or faster up to Max. Ball limit `Level` (default) releases exactly the level's
  launcher counts; `Auto` and `Max` keep every color streaming at that rate, `Auto` until memory is full and `Max`
  on past that into the page file or swap.
- **Three-section screen.** The board, the user interaction section (the only place that takes gameplay input)
  and a debug panel with FPS, min FPS, total objects, balls per color and the physics backend.
- **Map editor.** Paint multi-cell bricks, set hits and colors, edit and auto-fill launchers, validate, undo/redo,
  save and play. Saving a level's files while it plays reloads it within about 0.4 s.
- **Effects.** Bricks flash, slide into gaps and burst; a ball that hits its brick leaves a ring and shrinks away.

## Controls

| Where | Input | Action |
|---|---|---|
| Normal mode | Left-click a launcher | Release its balls |
| Performance mode | Start button | Begin automatic release |
| Any game | Header Pause button | Pause menu overlay |
| Pause overlay | Resume / Restart / Settings / Main Menu | Select action |
| Menus | Esc | Back |
| Editor | L-drag / R-drag | Apply tool / erase |
| Editor | 1-7, P/E/X/K | Color, tool |
| Editor | Ctrl+Z / Ctrl+Y / Ctrl+S / Esc | Undo / redo / save / back |

## Levels

Levels are `<Name>_Bricks.txt` and `<Name>_SpawnBall.txt` pairs in `data/assets/levels/normal` and
`data/assets/levels/performance`, in a simple text format: see [level format](data/docs/level-format.md).

## Project layout

```
data/assets/          fonts and level files
externals/            prebuilt third-party libraries (wgpu-native for Android)
data/docs/            build guide, level format, physics notes and measurements
source/core/          engine-agnostic game, level, editor and simulation code (no SDL, no engine types)
source/backends/      physics plugins: cpu (Scalar, AVX2, Zen4), cuda, rocm, vulkan
source/frontends/     sdl, unreal and unity front ends
source/tests/         unit tests (balls_tests)
source/tools/         benchmarks and measurement tools
scripts/              SDK fetch, shader compilers, benchmark drivers
build.bat, build.sh   the one build entry point for every front end and platform
```

## Git workflow

- `main` is the only trunk. Side work goes on `feature/*`, `fix/*`, `perf/*`, `refactor/*`, `build/*` or `docs/*`
  branches, merged into `main` with `--no-ff`.
- Commit titles use `feat` / `fix` / `perf` / `refactor` / `build` / `docs` / `test` / `chore` with a scope, for
  example `fix(physics): ...`, and one logical change per commit.
