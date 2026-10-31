# The scoring build and the target PC

The submission is built for one machine: the target PC. This page explains what that build contains, why
it is shaped the way it is, and what was measured on that PC. Other machines and the extended build are covered in
[building.md](building.md), [hybrid-physics.md](hybrid-physics.md) and [physics-backends.md](physics-backends.md).

## The machine

| Part | Target PC | What it means for the game |
|---|---|---|
| CPU | Intel i9-14900K: 8 P-cores and 16 E-cores, 32 threads | AVX2 and FMA everywhere; no AVX-512 |
| RAM | 64 GB DDR5 | room for billions of 8-byte balls once they stop being stepped |
| GPU | GTX 1650: Turing TU117, `sm_75`, 14 SMs, 4 GB, 75-82 W board power limit | memory bandwidth bounds the step; the power limit bounds sustained brick-heavy steps |
| Display | landscape monitor | the scoring build always lays out in landscape |

Two limits follow from this hardware, and the design is built around them:

- **While bricks are alive, the GTX 1650's memory bandwidth sets the ball count.** A step reads and writes every
  ball. On an empty board the CUDA kernel moves 153.8 GB/s, 93.7% of the card's measured copy bandwidth
  ([cuda-limits.md](cuda-limits.md), measured on a GTX 1650 in the development PC). Under sustained brick contacts the card also hits its board power limit and lowers
  its clock by 30-40%, so instructions per ball matter too.
- **Once the board is cleared, memory size sets it.** A ball that only meets walls follows a closed-form path and is
  not stepped at all ([free-flight.md](free-flight.md)). From then on the count is limited by how many balls fit in
  64 GB of RAM plus 4 GB of VRAM, not by time.

## What the scoring profile builds

`build.bat` with no arguments builds the SDL front end with the NVRHI renderer and `BALLS_PROFILE=scoring`, the
default on x86-64 Windows and Linux (root `CMakeLists.txt`). The profile defines `BALLS_FIXED_TARGET`, which the core
and the SDL front end test to drop everything the target PC doesn't need.

| | Scoring build | Why |
|---|---|---|
| Physics | the CUDA+AVX2 pair only (`HybridBackend`), chosen at start-up (`chooseStartupBackend`) and the only kind offered (`selectableBackends`) | the target PC has one NVIDIA GPU and one CPU, and the pair uses both |
| CPU code | whole program compiled with `/arch:AVX2` (`-mavx2 -mfma -mtune=raptorlake` on GCC/Clang), `/O2 /fp:fast` | the target CPU is known, so AVX2 needs no run-time dispatch |
| CUDA code | `sm_75` cubin only (`BALLS_SCORING_CUDA_SM`, passed to `balls_ptxgen` as `--only-sm=75`), compiled by NVRTC at build time | no JIT at start-up, no CUDA toolkit on the target PC |
| Memory | every ball allocated once per process, before play | balls never grow, copy or page-fault during play |
| Not built | Vulkan, ROCm and Zen4 physics, the Unity plugin | the target PC can't use them |
| Not shown | backend menu, machine benchmark, Layout choice, Ball collisions option | one backend and one layout; collisions need the Scalar backend, which isn't offered |
| Rendering | SDL3 + NVRHI, Vulkan by default (`--api d3d12` for Direct3D 12) | one GPU driver stack shared with nothing else |

Any other PC can run the scoring build too: it then falls back to AVX2 alone when there is no NVIDIA GPU.

## One frame

Three threads take part:

- The **UI thread** handles input and draws the latest snapshot of the game.
- The **physics thread** (`SessionRunner`) runs posted commands, adds balls, and steps the simulation in fixed
  1/120 s ticks, at most 4 per frame.
- The **backend worker** builds and frees backends, so allocating and releasing gigabytes never lands on a frame.

On the physics thread, `Simulation::tick` calls `PhysicsBackend::step` once. For the pair, that one call steps the
CPU's share of the balls on a helper thread while the calling thread drives CUDA, then merges the hits.

**Auto** (Performance mode's ball target) is the preallocated capacity; there is no probe. `AdditionGovernor` decides
how many balls each frame may add without missing 60 FPS:

| Rule | Value |
|---|---|
| Share of 1/60 s the physics frame plans to use | 50% while stepping, 85% in free flight |
| Most of that budget charged to the rest of the frame (drawing) | half |
| Cost model | last step's cost per ball and last addition's cost per ball, both linear; at most the live count added per frame |

## Physics on the pair

### Memory, allocated once

- **GPU:** all free VRAM less 512 MB, at 16 bytes per ball (the balls and their sorted copies). The arrays sit in
  reserved virtual address space (`GrowableDeviceArray`), so nothing moves.
- **CPU:** available RAM less a margin (1 GB or 1/32 of RAM), at 8 bytes per ball. `ReservedArray` reserves the
  address space, commits it in 64 MB granules and pre-faults the pages ahead on a helper thread.
- One preallocated pair lives for the whole process. Releasing it parks it, cleared, and the next session takes it
  back instead of allocating a second one (`PreallocatedPair` in `BackendFactory.cpp`).
- Every GB free at start-up holds about 67 million balls, and nothing opened later changes the budget. On the scoring
  PC, Auto measured 7.07-7.29 billion balls, depending on what else was running.

### One ball in 8 bytes

The GPU and the CPU store a ball the same way: a 32-bit position (16-bit x and y over the board) and a 32-bit state
(13-bit direction components, colour, bounce count). Speed isn't stored, because every ball has the same speed. A
step reads 8 bytes per ball and writes them back. That is 16 bytes of traffic per ball per tick, down from 52 in the
first CPU version. Requantising positions every tick uses a hashed dither, so rounding doesn't drift.

### The GTX 1650 side

- **Hits on the device.** Balls in a warp that hit the same brick find each other with `__match_any_sync`. One lane
  takes their hits with a single `atomicSub` and refunds any excess. Per-ball atomic loops on a few hot bricks had
  made 150 ms steps.
- **Cell sort.** Balls are sorted by board cell inside the step kernel (count, scan, then a fused step-and-sort).
  Unsorted, nearly every warp held a ball near a brick and ran the slow path at a fraction of its lanes. `SortSchedule`
  tries sorting never, every 8 or every 16 ticks and keeps the cheapest.
  - At 60M balls, a brick-level step went from 15.5 to 9.76 ms.
  - Warp-instructions per tick fell from 850M to 403M, which also helps under the power cap.
- **Compaction on the device.** Two kernels fill the holes left by dead balls with balls from the tail, with no host
  round trip.
- **Drawing cost independent of ball count.** Balls bid for screen-sized visibility-grid slots with one `atomicMax`
  on a 64-bit slot, and only the grid is read back. In free flight only sampled balls are evaluated: about 0.05 ms of
  GPU time per frame at any count.

What was tried on this GPU and reverted is listed in [cuda-limits.md](cuda-limits.md), "What did not help".

### The i9-14900K side

- **8 balls per AVX2 instruction.** Walls use compare-and-blend. One gather of the near-brick map skips the contact
  search for a whole group of 8, and the brick contact search runs on gathers. Rare cases (embedded contacts, the
  line-of-sight walk for retargeting) run scalar, only for the lanes that need them.
- **Fewer writes.** State is written back only when a bounce count changed. Render words go out through streaming
  stores.
- **Work split.** Balls are stepped in chunks of 16,384 on a worker pool with one thread per hardware thread, less one
  in the pair (`shareCoresWithGpu`). The pool runs at background priority, so the thread driving CUDA and the graphics
  driver always get a core.
- **Contention.** Only a hashed sample of balls bids for visibility-grid slots. When every ball wrote the shared
  ~45 KB grid, its cache lines bounced between cores and throughput halved (610M to 1,200M ball-steps/s in game).
- **Tuned on the target PC.** Chunk size, render samples, spare threads and pinning the physics thread to P-cores
  can be overridden from the environment (`BALLS_STEP_CHUNK`, `BALLS_RENDER_SAMPLES`, `BALLS_HYBRID_SPARE_THREADS`,
  `BALLS_PIN_LATENCY_THREADS`). Swept on this CPU, no variant beat the defaults beyond run-to-run noise (about 11%),
  so the defaults stay ([edge-cases.md](edge-cases.md), "CPU tuning for the target PC").

### Splitting the balls

| Rule | Value | Why |
|---|---|---|
| CPU share | starts at 8%, at most 50%; follows GPU cost per ball / (CPU + GPU cost per ball), smoothed | the slower side sets the step time, so both should finish together |
| Below this, the GPU takes everything | 262,144 balls | a split only adds overhead |
| Rebalance when the CPU is off its target by | 2% or 131,072 balls | moves are not free |
| Most balls moved per step | 1M, as raw 8-byte records | 4M took about 30 ms on the target PC, more than a 60 FPS frame |
| Brick hits | CUDA consumes its own on the device; the CPU's go to `Simulation`, which accepts them against what the GPU left and re-uploads the counts | one board, consistent counts |
| Board changes | sent to both sides as sparse cell updates | whole-board uploads were the frame on large boards |
| Drawing | both fill the same visibility grid; a CPU ball wins an occupied slot with probability equal to the CPU share | every ball shows in proportion |

In free flight neither side steps, so there is nothing to rebalance. New balls go to each side by its share while it
has room, then wherever there is room.

## Measured on the target PC

Maximum balls at 60 FPS with the capacity probe, `DA_1000Bricks` ([free-flight.md](free-flight.md); "CUDA+AVX2" is
the pair):

| Backend | Bricks alive (stepping) | Board cleared (free flight) |
|---|---|---|
| CUDA alone | 150M | 287M at ~4,500 FPS (before host-staged growth) |
| CUDA+AVX2 pair | 262M | 4.37 billion at ~2,600 FPS, min 233 |

Generated stress boards, balls held at 60 FPS ([edge-cases.md](edge-cases.md); the "Hybrid" column there is the pair):

| Board | AVX2 | CUDA | Pair |
|---|---|---|---|
| Empty or Solid, 16x16 to 128x128 | 7B (memory) | 386-403M (memory) | 7.3-7.4B (memory) |
| LoneBrick 128x128 | 73M | 114M | 175M |
| Tough 2048x2048 (4M bricks hit constantly) | 8M | 121M | 174M |
| Cage 512x512 (every ball trapped in a few cells) | 3M | 46M | 55M |

Frame time at 60M balls (150M on 512x512 boards) ([hybrid-physics.md](hybrid-physics.md), "Hit-heavy small boards"),
measured with Hybrid (`AVX2@cpu+CUDA@gpu0`), the device composer of the extended build, which holds the same two
devices as the pair:

| Board | CUDA alone | AVX2 + CUDA |
|---|---|---|
| Shelves 48x48 | 8.84-8.91 ms | 7.98 ms |
| LoneBrick 512x512 | 15.66-15.72 ms | 9.62-9.79 ms |
| Tough 512x512 | 16.77-16.85 ms | 14.78-15.03 ms |

The pair pays off most in two places:

- **Free flight:** the CPU side adds the 64 GB of RAM to the 4 GB of VRAM that hold balls.
- **Few contacts** (a lone brick): the GTX 1650 is memory-bound there, so the balls the CPU steps are added
  throughput.

 On boards where nearly every ball hits a brick every tick, CUDA alone is within a
few percent of it. Those steps are limited by the hits, and the CPU's hits go through the host.

## On the target PC itself

These are settings of the machine, not code:

- **Close other programs before starting the game,** GPU-heavy ones especially. The memory budget is measured once,
  at start-up.
- **Turn off Windows core parking** (a power-plan setting). With parking on, run-to-run noise in the measurements
  above grew.
- **Building on it:** MSBuild's default MSVC toolset there (14.34) is installed without its libraries. The build's
  setup check finds the complete 14.44 toolset and passes it to MSBuild (`build.bat`, root `CMakeLists.txt`).
