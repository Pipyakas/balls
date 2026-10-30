# Burst Ball-Step Port (Balls)

This directory contains the Unity Burst / DOTS implementation of the ball-stepping physics simulation for the **Balls** project, ported directly from the engine-agnostic C++ reference kernel (`source/core/sim/kernels/BallStepCore.h`).

---

## 1. Source Mapping Table

Every function, constant, and struct from [`source/core/sim/kernels/BallStepCore.h`](../../../../../../../core/sim/kernels/BallStepCore.h) is ported one-to-one into [`BallStep.cs`](BallStep.cs).

| C++ Reference (`BallStepCore.h`) | Line in C++ | C# Port (`BallStep.cs`) | Purpose / Description |
|---|---|---|---|
| `struct StepParams` | 10–27 | `struct StepParams` | Simulation step configuration passed to ball-step kernels |
| `struct BoardView` | 29–46 | `struct BoardView` | Read view over padded cells, brick colors, targets, and near flags |
| `BoardView::isNearBrick(int)` | 35 | `BoardView.isNearBrick(int)` | Checks if a padded cell is adjacent to or contains a brick |
| `BoardView::occupant(int, int&)` | 37–41 | `BoardView.occupant(int, out int)` | Reads brick index and brick color at a given cell index |
| `BoardView::targetCell(int, int, int)` | 43–45 | `BoardView.targetCell(int, int, int)` | Looks up target cell for nearest brick of matching color |
| `noBrickHit` | 48 | `BallStep.noBrickHit = -1` | Sentinel value indicating no brick was hit on step |
| `renderCoordinateMax` | 49 | `BallStep.renderCoordinateMax = 16383u` | Coordinate quantization maximum (14-bit integer, 0..16383) |
| `renderCoordinateBits` | 50 | `BallStep.renderCoordinateBits = 14u` | Bits per coordinate axis in packed render format |
| `renderColorShift` | 51 | `BallStep.renderColorShift = 28u` | Bit shift for 4-bit color in packed render format |
| `emptyRenderSlot` | 52 | `BallStep.emptyRenderSlot = 0xFFFFFFFFu` | Sentinel for empty render buffer slots |
| `packRenderCoordinate(float, float)` | 54–58 | `BallStep.packRenderCoordinate(float, float)` | Clamps and scales coordinate float to 14-bit unsigned int |
| `packBallForRender(...)` | 60–63 | `BallStep.packBallForRender(...)` | Packs (x, y, color) into a single 32-bit word: bits 0–13 x, 14–27 y, 28–31 color |
| `renderPackScale(float)` | `PhysicsBackend.h:73` | `BallStep.renderPackScale(float)` | Computes quantization scale factor `16383.0f / fieldExtent` |
| `clampFloat(float, float, float)` | 65–67 | `BallStep.clampFloat(float, float, float)` | Scalar float clamping helper |
| `paddedCellIndex(int, int, int)` | 69–71 | `BallStep.paddedCellIndex(int, int, int)` | Converts `(col, row)` to row-major index in 1-cell padded grid |
| `pushOutOfCell(...)` | 73–95 | `BallStep.pushOutOfCell(...)` | Pushes ball out of a cell boundary and calculates exit normal |
| `retargetTowardNearestBrick(...)` | 98–123 | `BallStep.retargetTowardNearestBrick(...)` | Retargets velocity vector toward target brick cell on bounce threshold |
| `reflectVelocity(...)` | 125–130 | `BallStep.reflectVelocity(...)` | Reflects 2D velocity against collision normal |
| `resolveEmbeddedContact(...)` | 134–145 | `BallStep.resolveEmbeddedContact(...)` | Pushes embedded ball out of cell, reflects velocity, and retargets if threshold reached |
| `stepBall(...)` | 148–251 | `BallStep.stepBall(...)` | Advances ball for deltaTime, resolves wall bounces, and checks 4 candidate brick cells |
| `stepBallSubsteps(...)` | 254–263 | `BallStep.stepBallSubsteps(...)` | Steps ball across `params.substeps` substeps; terminates on first hit |
| `freeFlightPhaseUnits` | 269 | `BallStep.freeFlightPhaseUnits = 4294967296.0f` | Fixed-point scale for 32-bit phase (`2^32`) |
| `freeFlightPhaseHalf` | 270 | `BallStep.freeFlightPhaseHalf = 0x80000000u` | Half-period bit boundary for reflection direction determination |
| `freeFlightSpan(float, float)` | 272–275 | `BallStep.freeFlightSpan(float, float)` | Usable travel distance between bounding walls `(extent - 2 * radius)` |
| `freeFlightPhase(...)` | 277–284 | `BallStep.freeFlightPhase(...)` | Unfolds triangle wave position and velocity into 32-bit phase |
| `freeFlightAdvance(...)` | 286–289 | `BallStep.freeFlightAdvance(...)` | Computes per-tick 32-bit phase advance for a given velocity |
| `foldFreeFlightPhase(...)` | 292–299 | `BallStep.foldFreeFlightPhase(...)` | Folds 32-bit phase back into physical coordinate and direction |
| `freeFlightWallCrossings(...)` | 302–307 | `BallStep.freeFlightWallCrossings(...)` | Calculates number of half-periods crossed via 64-bit integer shift |
| `struct FreeFlightPosition` | 309–315 | `struct FreeFlightPosition` | Position, velocity, and wall crossings after free-flight travel |
| `flyFreely(...)` | 318–334 | `BallStep.flyFreely(...)` | Evaluates closed-form ball state after flying freely for `ticks` |
| `flyFreelyBackwards(...)` | 338–344 | `BallStep.flyFreelyBackwards(...)` | Calculates initial ball state from a future state flying backwards |

---

## 2. Differences Between C++ and Burst Implementation

| Aspect | C++ Reference (`BallStepCore.h`) | Burst Port (`BallStep.cs` / `BurstBallStore.cs`) | Why / Justification |
|---|---|---|---|
| **Board View Container** | C++ template `template <typename Board>` with raw pointers `const int*` | Concrete `struct BoardView` holding `[ReadOnly] NativeArray<int>` | Unity Burst requires blittable unmanaged types; `NativeArray<int>` provides safe memory bounds checking in the Editor and compiles to raw pointer offsets under Burst without template compilation overhead. |
| **Function Inlining Annotations** | `BALLS_HOST_DEVICE` (`inline` or `__device__ __forceinline__`) | `[MethodImpl(MethodImplOptions.AggressiveInlining)]` and `[BurstCompile]` | In C#, `MethodImplOptions.AggressiveInlining` signals the compiler to inline sub-functions (such as `pushOutOfCell`, `reflectVelocity`, and `clampFloat`) directly into `stepBall` and `stepBallSubsteps`. |
| **Math Library** | `<cmath>` (`fminf`, `fabsf`, `floorf`, `sqrtf`) | `Unity.Mathematics.math` (`math.min`, `math.abs`, `math.floor`, `math.sqrt`) | Burst enforces `Unity.Mathematics` for vector and scalar intrinsics; `System.Math` methods use managed double-precision calls which are slower and may inhibit SIMD vectorization. |
| **Ball Storage Layout** | `CpuBallStore` in C++ used Structure of Arrays (SoA: separate `SimdVector<float>` per channel) | `BurstBallStore` uses Array of Structures (AoS: `NativeArray<BallData>`) | **SoA in C++** was designed specifically for explicit 8-wide AVX2 registers (`__m256`) in `Avx2Backend.cpp`. **In Unity Burst**, parallelization is achieved across CPU cores via `IJobParallelFor`. Each ball is 24 bytes (4 floats, 2 ints). A single 64-byte L1 cache line holds 2.66 balls. AoS ensures that all 6 fields needed by `stepBall` are loaded in a single cache line access with zero strided cache misses, while reducing allocation, resizing, and disposal down to a single `NativeArray`. |
| **Parallel Hit Reporting** | `std::vector<BallHit>& hits` appended sequentially in single-threaded loop | `NativeQueue<BallHit>.ParallelWriter` | In multi-threaded `IJobParallelFor` execution, multiple worker threads detect hits concurrently. A lock-free `NativeQueue.ParallelWriter` allows contention-free recording of hits matching the C++ `{ uint ball; int brick; }` layout. |

---

## 3. StepParams Mapping

Step parameters are constructed in C++ by `Simulation::stepParams(int substeps)` (`source/core/sim/Simulation.cpp:113-126`). Below is the exact field mapping:

| Field Name | C++ Type | C# Type | Source Value / Calculation in `Simulation.cpp` | Semantic Meaning |
|---|---|---|---|---|
| `deltaTime` | `float` | `float` | `settings.tickSeconds` (default: `1.0f / 120.0f`) | Duration of one physics simulation tick in seconds. |
| `radius` | `float` | `float` | `settings.ballRadius` (default: `0.2f`) | Ball collision radius in grid cell units. |
| `speed` | `float` | `float` | `settings.ballSpeed` (default: `14.0f`) | Constant ball speed in grid cell units per second. |
| `fieldWidth` | `float` | `float` | `static_cast<float>(brickBoard.columns())` | Width of the playable board in cell units. |
| `fieldHeight` | `float` | `float` | `static_cast<float>(brickBoard.rows())` | Height of the playable board in cell units. |
| `paddedWidth` | `int` | `int` | `brickBoard.paddedWidth()` (`columns + 2`) | Stride of padded cell rows including left and right padding rings. |
| `paddedCellCount`| `int` | `int` | `brickBoard.paddedCellCount()` (`(columns+2)*(rows+2)`) | Total number of cells in the 1-cell padded grid buffer. |
| `retargetBounceThreshold` | `int` | `int` | `settings.retargetBounceThreshold` (default: `3`) | Number of consecutive wall/brick bounces before ball retargets toward nearest matching brick. |
| `packScaleX` | `float` | `float` | `renderPackScale(columns)` (`16383.0f / columns`) | Coordinate scale factor for quantization of x into bits 0–13. |
| `packScaleY` | `float` | `float` | `renderPackScale(rows)` (`16383.0f / rows`) | Coordinate scale factor for quantization of y into bits 14–27. |
| `substeps` | `int` | `int` | `substeps` (default: `1`) | Number of sub-tick substeps to advance within `stepBallSubsteps`. |
| `tick` | `unsigned int` | `uint` | `elapsedTicks` | Simulation tick counter since session start. |
| `freeFlight` | `int` | `int` | `isCleared() && !ballCollisionsActive() ? 1 : 0` | Nonzero when all bricks are cleared and ball collisions are disabled. |

---

## 4. Deliverables Summary

1. [`BallStep.cs`](BallStep.cs)
   - Contains `StepParams`, `BoardView`, `FreeFlightPosition`, and the `BallStep` static utility class.
   - Exact parity with `BallStepCore.h`: exact mathematical formulas, reflection rules, retargeting logic, coordinate packing, and closed-form free flight helpers.
   - Includes PascalCase aliases for standard C# idioms while keeping exact original lowercase names.

2. [`BurstBallStore.cs`](BurstBallStore.cs)
   - Contains `BallData` (24 bytes) and `BallSpawn` (20 bytes).
   - `BurstBallStore` unmanaged struct:
     - `AddBall(float x, float y, float vx, float vy, int color, int bounces = 0)`
     - `AddBall(BallSpawn spawn)` / `AddBalls(...)`
     - `RemoveDescendingIndices(...)` implementing the descending swap-back removal algorithm.
     - `Truncate(int newLimit)` and `Truncate(int newLimit, NativeArray<ulong> removedTally)`.
     - `PackForRender(NativeArray<uint> destination)` producing the exact 32-bit packed layout expected by `BallRenderSystem` and shaders (`Balls/Procedural`).
     - `Count`, `Capacity`, `EnsureCapacity`, `Clear`, and `Dispose`.

3. [`BurstBallStepJob.cs`](BurstBallStepJob.cs)
   - `BallHit` struct `{ uint ball; int brick; }`.
   - `BurstBallStepJob`: `IJobParallelFor` stepping balls using `BallStep.stepBallSubsteps`, collecting hits into `NativeQueue<BallHit>.ParallelWriter`, with optional direct render packing.
   - `BurstFreeFlightJob`: `IJobParallelFor` evaluating closed-form wall reflections using `BallStep.flyFreely`.
   - `BurstPackBallsJob`: `IJobParallelFor` for parallel render packing.

---

## 5. Burst & Compiler Safety Notes

- **Zero Managed Allocations**: All three code files use only unmanaged structs, `NativeArray`, `NativeSlice`, `NativeQueue.ParallelWriter`, and `Unity.Mathematics` primitives. No LINQ, no `class` references inside jobs, and no `UnityEngine.Object` types are present.
- **Assembly Integration**: All files reside under `Assets/Balls/Runtime/Physics/Burst` within the existing `Balls.Runtime` assembly definition (`Balls.Runtime.asmdef`), which already references `Unity.Burst`, `Unity.Collections`, and `Unity.Mathematics` with `allowUnsafeCode: true`.
- **Burst Compilation**: Both jobs are decorated with `[BurstCompile(FloatMode = FloatMode.Fast, FloatPrecision = FloatPrecision.Standard, CompileSynchronously = true)]`.
- **Potential Compilation Caveats for Composer**:
  - `NativeQueue<T>.ParallelWriter`: Supported in all versions of `com.unity.collections` (1.x and 2.x).
  - `NativeArray<T>.Copy`: Standard in `Unity.Collections`.
  - In `BurstBallStore.EnsureCapacity`, reallocation calls `Dispose()` on the old array and reassigns `m_Balls`. If `BurstBallStore` is passed to Burst jobs or stored as a value struct, mutations to capacity must be performed via `ref BurstBallStore` or from the main thread before scheduling jobs.
