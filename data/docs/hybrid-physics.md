# Hybrid and CUDA+AVX2 split physics

Two backends split the balls between devices. `CUDA+AVX2` is the fixed pair described first. `Hybrid` is the
general device composer: one backend on each device the player picks, described in
[The device composer](#the-device-composer).

## CUDA+AVX2

Balls never collide with each other, so the ball set can be split between devices. The `CUDA+AVX2` backend
(F4, `/backend?kind=cuda-avx2`; called `Hybrid` until the general device composer took that name) keeps most balls on CUDA and a share on the AVX2 backend, steps both at the same
time, and merges their results on the host. Measured on a GTX 1650 with an i5-12400F (6 cores, 12 threads,
dual-channel DDR4-3200).

### How it works

- `HybridBackend` (`source/core/sim/`) owns a CUDA and an AVX2 backend. Each step runs the AVX2 step on a helper
  thread while the calling thread drives CUDA.
- The CPU share follows the measured cost per ball of each side: `share = gpuCost / (cpuCost + gpuCost)`,
  smoothed over frames. When the CPU ball count drifts more than 10% from its target, up to 256k balls move
  between devices per step as raw 8-byte records (`takeCompactTail` / `addCompact`), about 0.4-1 ms per move.
- Brick hits: CUDA resolves its hits on the device as before. The CPU returns hits to `Simulation`, which
  accepts them against the hits the GPU left, then re-uploads the hit counts so the GPU sees them next tick.
- Rendering: both sides fill the same visibility grid. The CPU writes its grid in the step; the hybrid
  overlays it on the downloaded GPU grid, letting a CPU ball win an occupied slot with probability equal to the
  CPU share.
- The AVX2 pool drops one worker and runs at below-normal priority in hybrid mode, so the thread waiting on
  CUDA and the graphics driver threads always get a core.

### What made the CPU side worth adding

The first hybrid was slower than CUDA alone. Three changes fixed it:

| Change | AVX2 throughput (2 substeps) |
|---|---|
| Original: seven float/int arrays, 28 bytes read and 24 written per ball | 650 M ball-steps/s |
| Compact 8-byte ball (CUDA's encoding), state written only when it changes, near-brick skip | 1080 M (bricks), 1250 M (empty board) |
| Sampled visibility-grid bids (8 per slot) instead of every ball writing the shared grid | in-game 610 M -> 1200 M |

The last one matters only in the game: eleven threads storing every ball into the same ~45 KB grid made its
cache lines bounce between cores and halved CPU throughput.

### Results

`balls_bench --scatter` (full `Simulation`, scattered balls, empty board after the first ticks):

| Balls | CUDA | CUDA+AVX2 | Gain |
|---|---|---|---|
| 96M, 3000 ticks, 2 substeps | 10.29 ms/tick | 9.58 ms/tick | 7% (9.2 ms steady, 11%) |

In the game, hidden window, 140M balls, three alternating runs each:

| Backend | FPS | Frame |
|---|---|---|
| CUDA | 67.4 | 14.83 ms |
| CUDA+AVX2 | 68.7 | 14.63 ms |

In-game the gain shrinks to about 2%, within run-to-run noise, for two reasons:

- At 60-70 FPS with 120 Hz ticks, frames alternate between one and two substeps. The CPU's cost scales with
  substeps (about 7 vs 13 ms for its share) while the memory-bound GPU's barely does (11.2 vs 13.4 ms), so no
  single split balances both kinds of frame.
- Each frame waits for the slower side. Averaged separately, both sides take about 13.8 ms, but the
  per-frame maximum averages about 15 ms.

The CPU can only ever add its own throughput: about 1.2 G ball-steps/s against the 1650's 9-10 G. On a CPU with
more cores and faster memory the share grows, but the GPU still carries most of the balls.

## The device composer

`Hybrid` (`DeviceComposer`, `source/core/sim/`) runs one backend per picked physical device, for example
`AVX2@cpu+CUDA@gpu0+Vulkan@gpu1`, and splits the balls between all of them. It is separate from the CUDA+AVX2
pair, which stays as its own kind.

### Devices

Plugin interface 4 adds `ballsDeviceCount`, `ballsDeviceInfo` (name, type, UUID, PCI location, memory),
`ballsDeviceUnavailableReason`, `ballsDeviceDetail` and `ballsCreateBackendOnDevice`. `DeviceInventory` merges the
plugins' lists into physical devices: `cpu`, then `gpu0`, `gpu1`, ... in PCI order. A GPU that CUDA and Vulkan both
list is one device, matched by UUID (`cuDeviceGetUuid` and `VkPhysicalDeviceIDProperties::deviceUUID` are the same
bytes), so a selection can give it only one backend. `balls_bench --devices` prints the inventory.

- CUDA keeps one primary context and kernel module per device, and every backend makes its own context current
  before it calls the driver, since the composer drives several devices from the same threads.
- Vulkan shares one `VkInstance` and initialises Vulkan-Hpp's global dispatcher at instance level only, so the
  device calls NVRHI makes go through the loader's trampolines, which dispatch on the `VkDevice`. Re-initialising
  it per device, as before, is unsafe with two GPUs from different drivers.

### Splitting the balls

- Parts are numbered device-resolving first (CUDA, Vulkan), then host-reporting (CPU); each part's balls follow the
  previous parts' in the index space `Simulation` sees, so host hits and removals map back by offset.
- All parts step at once, each non-lead part on a helper thread. Each part's share follows its measured balls per
  millisecond, smoothed, starting from a guess (GPU 1, Vulkan 0.8, iGPU 0.2, CPU 0.1). Balls move between parts as
  8-byte records, up to 4M per step, when a part is more than 2% or 131072 balls off its target.
- Below 262144 balls everything runs on the lead part.
- A part whose fixed cost per step exceeds what its share saves is parked: when the composed step stays more than
  3% slower than the lead part alone would be for all balls, for 60 settled steps, the slowest part's balls move
  away and it stops stepping. On an RTX 3070 laptop this parks the CPU and the iGPU, which otherwise made the
  composer slower than CUDA alone.
- Snapshots: each part fills the same visibility grid; part k wins an occupied slot with probability (its balls) /
  (balls of parts 0..k), so every slot shows a ball from a part in proportion to that part's balls.
- Capacity is the sum of the parts'; a part whose backend reports no limit (Vulkan) is counted at its device memory
  / 16 bytes, so it cannot make the whole composer look unlimited to the machine benchmark.

### Bricks

A device-resolving backend consumes hits from its own copy of the brick counts (`tryConsumeHit`) and kills the ball
on the device. Two such parts would each consume a brick's hits and together kill more balls than the brick had.

With two or more device-resolving parts, every tick the composer splits each brick's remaining hits into budgets,
proportional to the parts' shares, with the units left after rounding drawn by hash. It sends each part its
budget (`uploadBrickHits`) only where it differs from that device's copy. After the step it subtracts what each
part consumed: CUDA lists the bricks it hit, and Vulkan returns its remaining counts. It then reports the true counts
to `Simulation`. The parts together never consume more than a brick has, so kills always equal hits taken off
bricks; `ComposerTests` checks this every tick and fails without the budgets. A part that runs out of budget on a
brick bounces the ball, as a device does when the brick is used up within a tick. Redrawing every brick each tick
keeps a last hit from staying with a part whose balls never reach it.

Host-reporting parts (CPU) take the pair's path: `Simulation` accepts their hits against what the devices left.

Cost, launch level, 2M balls, CUDA (RTX 3070) and Vulkan (Radeon iGPU) both resolving hits:

| Scheme | Budget time per step | Bricks left after 300 ticks (CUDA alone: 208) |
|---|---|---|
| All bricks split after any hit, Vulkan uploads in a submit of their own | 1.16 ms | 246 |
| Staged Vulkan writes, only bricks that lost hits split again | 0.023 ms | 644 |
| Staged Vulkan writes, all bricks redrawn every tick (current) | 0.013-0.044 ms | 216 |
