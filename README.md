# Balls

A ball-bouncing, brick-breaking color game for Windows, written in C++20. An
engine-agnostic core (`source/core/`) runs the game; the SDL3 front end (`source/frontends/sdl/`) draws it through
NVRHI (Vulkan or Direct3D 12) with Dear ImGui for the UI. The physics runs on interchangeable backends, from a
scalar reference to AVX2, CUDA and a split of the balls across every device: see
[physics backends](data/docs/physics-backends.md).

## Quick start

Requirements: Windows 10/11 x64, Visual Studio 2022 (C++ workload), CMake 3.25+, Python 3, and internet access on
the first build.

1. Double-click `build.bat`. It checks the PC, names anything missing, and builds `dist\win32-sdl\balls.exe`.
2. Run `dist\win32-sdl\balls.exe`.
3. For the most balls: Performance mode, level `Unlimited`, Spawn / sec `Max`, Ball count `x10000`, Ball limit
   `Auto`, then `Start`. Close other programs first: Auto is the RAM and VRAM free when the game starts.

The release is tagged (`git tag -l`).

## Tests

After `build.bat`:

```powershell
cmake --build build/win32-scoring --config Release --target balls_tests balls_bench
build/win32-scoring/bin/Release/balls_tests.exe
build/win32-scoring/bin/Release/balls_bench.exe --balls 2000,200000,1000000 --ticks 240
```

Other front ends (Unreal, Unity), Linux, Android, build options and the benchmark flags:
[building and testing](data/docs/building.md).

## Features

- **Normal mode.** Pick launchers from a 3-column queue; each releases its exact number of balls. Balls bounce off
  walls and bricks, break bricks of their own color, and home toward the nearest one after the third bounce.
- **Performance mode.** One click starts automatic release from seven launchers at once, at 10 to 2000 balls per
  second or Max, with a ball-count multiplier from x1 to x10000.
- **Three-section screen.** The board, the user interaction section (the only place that takes gameplay input)
  and a debug panel with FPS, min FPS, total objects, balls per color and the physics backend.
- **Map editor.** Paint multi-cell bricks, set hits and colors, edit and auto-fill launchers, validate, undo/redo,
  save and play. Saving a level's files while it plays reloads it within about 0.4 s.
- **Effects.** Bricks flash, slide into gaps and burst; a ball that hits its brick leaves a ring and shrinks away.

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
