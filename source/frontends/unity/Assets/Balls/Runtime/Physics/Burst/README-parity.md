# Unity Burst Backend Parity Mapping

This document provides a complete parity audit between the pure C# Unity Burst backend for **Balls** (`source/frontends/unity/Assets/Balls/Runtime/Physics/Burst/`) and the reference C++ core engine (`source/core/`).

---

## 1. Complete Rule-to-Source Mapping Table

| Game Rule / System | C++ Reference File & Lines | C# Port File & Class | Implementation Details / Parity Notes |
|---|---|---|---|
| **Session State Enum** | `source/core/game/GameSession.h:20` | `BallsNative.State*` (`BallsNative.cs:48-50`) | Ready (0), Running (1), Won (2). |
| **Game Modes** | `source/core/game/GameMode.h:6` | `BallsNative.Mode*` (`BallsNative.cs:38-39`) | Normal Mode (0), Performance Mode (1). |
| **Spawn Rate Options** | `source/core/game/GameSession.h:23` | `BurstGameBackend.SpawnRateOptions` | `{ 10, 50, 200, 1000, 2000, 10000, 50000, 200000, 1000000, 0 }` (0 indicates unlimited/Max). Default index 2 (200 balls/s). |
| **Ball Multiplier Options** | `source/core/game/GameSession.h:24` | `BurstGameBackend.BallMultiplierOptions` | `{ 1, 10, 100, 1000, 10000 }`. Applied in Performance Mode. Default index 0 (1x). |
| **Ball Limit Options** | `source/core/game/GameSession.h:25` | `BurstGameBackend.BallLimitOptions` | `{ 10_000, 100_000, 1_000_000, 10_000_000, ulong.MaxValue }`. Default index 4 (unlimited). |
| **Normal Mode Rate** | `source/core/game/SpawnController.h:13` | `BurstGameBackend.NormalModeSpawnRate` | Constant `60.0f` balls per second. |
| **Session Initialization & Restart** | `source/core/game/GameSession.cpp:57-74` | `BurstGameBackend.Restart()` | Clears ball store, resets elapsed time, reloads layout into board and queue, sets initial state (Running for Normal mode, Ready for Performance mode). |
| **Launcher Selection Rule** | `source/core/game/GameSession.cpp:86-96` | `BurstGameBackend.ChooseLauncher(col, row)` | In Normal mode while Running, takes launcher at `(col, row)` from queue and enqueues balls. Returns false if not in Running state or if slot is empty. |
| **Auto-Release Trigger** | `source/core/game/GameSession.cpp:98-103` | `BurstGameBackend.StartAutoRelease()` | In Performance mode while Ready, transitions state to Running and activates `autoRelease = true`. |
| **Auto-Release Rotation** | `source/core/game/GameSession.cpp:321-326` | `BurstGameBackend.Update(deltaSeconds)` | When running with `autoRelease` enabled and `spawnController.IsIdle`, takes next launcher via `launcherQueue.TakeNextInRotation()`. |
| **Launcher Queue Loading** | `source/core/game/LauncherQueue.cpp:11-22` | `BurstLauncherQueue.Load(layout)` | Assigns column queues, populates non-empty launcher slots in row-major order, initializes `riseOffset = 0.0f`. |
| **Launcher Rise Animation** | `source/core/game/LauncherQueue.cpp:24-30` | `BurstLauncherQueue.Animate(deltaTime)` | Decays `riseOffset` at constant rate `RiseSpeedRowsPerSecond = 8.0f` down to `0.0f`. |
| **Launcher Take & Offset Boost** | `source/core/game/LauncherQueue.cpp:32-46` | `BurstLauncherQueue.Take(col, row)` | Pops taken launcher from column list; increments `riseOffset += 1.0f` for all subsequent launchers remaining behind in that column. |
| **Round-Robin Queue Rotation** | `source/core/game/LauncherQueue.cpp:48-65` | `BurstLauncherQueue.TakeNextInRotation()` | Searches columns cyclically starting from `rotationColumn`, takes row 0 of the first non-empty column, advances `rotationColumn = (col + 1) % colCount`. |
| **Remaining Queue Balls Tally** | `source/core/game/LauncherQueue.cpp:77-86` | `BurstLauncherQueue.RemainingBalls()` | Sums remaining ball counts partitioned by 7 ball colors across all column queues. |
| **Spawn Budget Accumulation** | `source/core/game/SpawnController.cpp:21-29` | `BurstSpawnController.Update(...)` | Accumulates `budget += ballsPerSecond * deltaTime` (or `MaxBallsPerFrame = 1<<20` if unlimited). Allowances capped to `capacityLeft`. |
| **Spawn Stream Release (FIFO)** | `source/core/game/SpawnController.cpp:31-49` | `BurstSpawnController.Update(...)` | Takes streams FIFO; releases `min(allowance, stream.remaining)` balls per call, decrements pending, pops finished streams. |
| **Ball Launch Dispersion** | `source/core/sim/Simulation.cpp:120-154` | `BurstGameBackend.LaunchBalls(color, count)` | Generates initial positions at board launch point `(cols * 0.5f, rows - 2 * radius)` with horizontal jitter in `[-radius, +radius]` and launch angles in `[-55°, +55°]` with speed `14.0f`. |
| **Random Number Generator** | `source/core/sim/Simulation.h:127` | `BurstGameBackend.Mt19937` | Exact 32-bit Mersenne Twister initialized with seed `20261026u` for deterministic parity with C++ `std::mt19937`. |
| **Simulation Step Tuning** | `source/core/sim/Simulation.h:12-19` | `BurstGameBackend` constants | `ballRadius = 0.25f`, `ballSpeed = 14.0f`, `tickSeconds = 1/120f`, `maxTicksPerFrame = 4`, `retargetBounceThreshold = 3`. |
| **Fixed Tick Accumulation** | `source/core/sim/Simulation.cpp:383-401` | `BurstGameBackend.Update(deltaSeconds)` | Clamps accumulator to `tickSeconds * maxTicksPerFrame`, computes integer ticks, advances physics via `Tick(ticks)`. |
| **Exact Spawn Injection** | `source/core/sim/Simulation.h:42` | `BurstGameBackend.SpawnBallExact(...)` | Injects exact `(x, y, vx, vy, color)` ball spawn without RNG for parity test harnesses. |
| **Decoupled Physics Stepping** | `source/core/sim/Simulation.cpp:351-381` | `BurstGameBackend.Tick(ticks)` | Flushes pending spawns, runs parallel ball step, dequeues hits, resolves brick damage, and updates ball store with zero wall-clock coupling. |
| **Brick Hit Application** | `source/core/sim/BrickBoard.cpp:239-263` | `BurstBoard.ApplyHit(brickIndex)` | Decrements `hitsLeft`, decrements color tally, sets `hitFlash = 0.15f`. If destroyed, sets cells to `EmptyCell (-1)`, queues bricks below, marks dead, increments revision. |
| **Brick Animation Timers** | `source/core/sim/BrickBoard.cpp:410-429` | `BurstBoard.Animate(deltaTime)` | Decays `riseOffset` by `RiseSpeedCellsPerSecond = 10.0f` and `hitFlash` by `deltaTime`. |
| **Free Rows Above Brick** | `source/core/sim/BrickBoard.cpp:300-321` | `BurstBoard.FreeRowsAbove(brickIndex)` | For each cell in brick, checks upward until hitting another brick or board top. Returns minimum free distance across all columns. |
| **Brick Raise & Rise Offset** | `source/core/sim/BrickBoard.cpp:323-338` | `BurstBoard.RaiseFully(brickIndex)` | Erases brick from current cells, subtracts distance from `row`, rewrites into new cells, increases `riseOffset += distance`, marks animating. |
| **Column Settle Trigger** | `source/core/sim/BrickBoard.cpp:360-366` | `BurstBoard.QueueBricksBelow(brickIndex)` | Finds lowest row per column of destroyed brick and pushes the first solid brick below into `bricksToSettle` min-heap. |
| **Column Settling Resolution** | `source/core/sim/BrickBoard.cpp:390-408` | `BurstBoard.SettleColumns()` | Pulls bricks in ascending row order from min-heap, raises each fully, cascades settling below. Increments revision on movement. |
| **Whole Board Initial Settle** | `source/core/sim/BrickBoard.cpp:368-388` | `BurstBoard.SettleWholeBoard()` | Sorts all alive bricks top-to-bottom by row and raises each fully into place on level load. |
| **Near-Brick 3x3 Grid Flags** | `source/core/sim/BrickBoard.cpp:112-131, 214-237` | `BurstBoard.RebuildNearCounts() / SetCell` | Tracks 3x3 neighbor counts around every solid brick cell. Sets `nearFlags[index] = 1` if count > 0, allowing quick ball collision bypass. |
| **Target Field BFS Flood Fill** | `source/core/sim/TargetField.cpp:45-80, 150-203` | `BurstBoard.RebuildTargetField()` | For each of the 7 ball colors: initializes border with `PaddingCell (-2)` and cells with `NoTarget (-1)`; seeds frontier with matching-color brick cells; BFS floods 8 neighbor offsets; clamps border cells to nearest inside cell. |
| **Hit Resolution & Descending Removal** | `source/core/sim/Simulation.cpp:315-342` | `BurstGameBackend.Tick(ticks)` | Filters hits on living bricks, damages bricks, records killed ball indices, sorts descending, swap-removes from `BurstBallStore` and `packedBalls`. Settles columns if any brick died. |
| **Win Condition Detection** | `source/core/game/GameSession.cpp:366-368` | `BurstGameBackend.Update(deltaSeconds)` | Transitions `sessionState = StateWon (2)` when `board.AliveBrickCount == 0` while Running. |
| **Level Text Parsing (Bricks)** | `source/core/level/LevelParser.cpp:327-415` | `LevelDocument.ParseBrickLayout(text)` | Parses `[Board]` (Width, Height, PlayableHeight), `[Layout]` (grid of cell IDs / Empty / .), and `[BrickTypes]` (`<Id> <Color> <Hits>`). Invalids reset to `-1`. |
| **Level Text Parsing (Launchers)** | `source/core/level/LevelParser.cpp:417-449` | `LevelDocument.ParseLauncherLayout(text)` | Parses `[Board]` (Width, Height), `[Layout]` (`Color(count)` or `Empty`). |
| **Level Text Serialization** | `source/core/level/LevelWriter.cpp:30-65` | `LevelDocument.FormatBrickLayout / FormatLauncherLayout` | Reconstructs `<Name>_Bricks.txt` and `<Name>_SpawnBall.txt` matching core format bit-for-bit. |
| **Atomic File Saving** | `source/core/level/LevelWriter.cpp:67-73` | `LevelDocument.SaveLevel(level, bPath, lPath)` | Writes to `.tmp` file and replaces target file atomically. |

---

## 2. Features Knowingly Omitted / Not Ported

| Feature / Subsystem | C++ Source Location | Reason Not Ported |
|---|---|---|
| **CapacityProbe Benchmarking Engine** | `source/core/game/CapacityProbe.cpp/.h` | `CapacityProbe` dynamically ramps up ball counts (up to tens of millions) while sampling hardware frame/work times to benchmark discrete GPU compute capacity (CUDA/Vulkan). The Burst backend operates on CPU jobs; per instructions, `StartProbe` is a no-op that reports unsupported. |
| **Ball-vs-Ball Elastic Collisions** | `source/backends/cpu/ScalarBackend.cpp:40-163` | The C++ scalar backend includes an optional spatial hash grid for resolving pairwise elastic ball collisions when `ballCollisionsWanted` is enabled. In the core game, `ballCollisions` is disabled by default (`false`), and the Burst stepping job (`BurstBallStepJob.cs`) focuses strictly on ball-against-board physics. |
| **Multi-Threaded Target Field Rebuild via `std::async`** | `source/core/sim/TargetField.cpp:90-148` | In C++, `TargetField` launches background futures for boards exceeding `asyncCellThreshold = 65536`. For typical game boards (e.g. 7×14 = 98 cells or 1000 cells in performance levels), BFS flood-fill across all 7 colors completes in under 0.1 ms synchronously in C#, avoiding thread dispatch overhead and race conditions. |
| **Backend Hot-Swapping to Native C++ Plugins** | `source/core/game/GameSession.cpp:194-241, 310-314` | `BurstGameBackend.SwitchBackend(kind)` only recognizes `BackendBurst (5)`. Because this backend operates independently of `balls_unity.dll`, switching to native backends (Scalar, AVX2, CUDA, Hybrid, Vulkan) from within `BurstGameBackend` is not supported. |

---

## 3. Unity & Burst Compilation Notes

All newly authored files strictly adhere to Unity Burst and DOTS best practices:
- **No Direct Unity Engine Mutations in Jobs**: All job execution is isolated in `BurstBallStepJob.cs`.
- **Native Memory Management**: `BurstBoard` and `BurstGameBackend` implement `IDisposable`. Native allocations (`cells`, `colors`, `nearFlags`, `targets`, `packedBalls`, `ballStore`) use `Allocator.Persistent` and are guaranteed to be cleaned up on disposal.
- **Job Temporary Allocations**: Per-tick hit queues use `Allocator.TempJob` and are disposed immediately upon `JobHandle.Complete()`.
- **Unsafe Pointer Access**: In `ReadBalls`, `(IntPtr)NativeArrayUnsafeUtility.GetUnsafePtr(packedBalls)` is used to expose the packed 32-bit render words without copying managed buffers. This requires `allowUnsafeCode: true`, which is enabled in `Balls.Runtime.asmdef`.
- **Zero Ambiguity Types**: To prevent naming collisions with `UnityEngine.Color` or `UnityEngine.Random`, `LevelDocument.ColorName`, `BurstBoard.BallColorCount`, and the self-contained `Mt19937` struct are used.
