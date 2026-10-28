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

### Results on two GPUs

Two-GPU desktop: Ryzen 7 7700 (8 cores, DDR5), RTX 2060 12 GB (CUDA, `gpu1`) and RX 6700 XT 12 GB (Vulkan through RADV,
`gpu0`), Linux. `balls_bench --grid`: raw steps on the 1000-brick level with the game's visibility grid, 1500
ticks, average including the first ticks while the shares settle. Physics FPS assumes two ticks per frame, as the
machine benchmark does, and leaves out rendering (the two-GPU desktop has no game build).

| Backend | 32M balls | 96M balls | Physics FPS at 96M |
|---|---|---|---|
| AVX2 | 1.12 G ball-steps/s | 1.16 G | 6 |
| CUDA (RTX 2060) | 17.8 G | 19.0 G | 99 |
| Vulkan (RX 6700 XT) | 15.4 G | 15.2 G | 79 |
| CUDA+AVX2 | 15.9 G | 15.2 G | 79 |
| Hybrid AVX2 + CUDA | 17.8 G (CPU parked) | 18.0 G (CPU parked) | 94 |
| Hybrid CUDA + Vulkan (RX) | **28.2 G** | **30.0 G** | **156** |
| Hybrid AVX2 + CUDA + Vulkan (RX) | 24.2 G (CPU parked) | 27.0 G (CPU parked) | 141 |
| Hybrid Vulkan (RX) + Vulkan (RTX) | 23.5 G | 26.5 G | 138 |

The two GPUs settle at about 58% / 42% (CUDA / RX) with equal step times; the composer adds about 0.1 ms per
step over its slowest part. The CPU, at a fifteenth of a GPU's throughput, is parked in every set. The brick
budgets cost 0.011-0.015 ms per step.

Two things made the two-GPU split pay. Running a CUDA and a Vulkan benchmark in separate processes at the same time
slows neither, but inside the composer the RX first ran four times slower per ball. The shares oscillated and
the composer moved 109M balls in 265 of 1500 steps, each move leaving the receiving GPU's balls unsorted. Once the
shares settle, a part now has to be 10% off its target before balls move, with 30 steps between moves. That took
the pair of GPUs from 15.7 G to 28.2 G.

The machine benchmark (balls at 60 FPS) is dominated by free flight there: the board clears and frames stop
growing with the ball count, so the score is the memory a backend can hold. Vulkan alone reports no memory limit
and wins with "no limit"; Hybrid CUDA + Vulkan scores 2.3 B (capped by device memory), CUDA+AVX2 3.1 B
(including CPU RAM).

## Zen4, ROCm and VulkanMulti

Three more backends can be composer parts.

- **Zen4** (`backends/cpu/Zen4Backend`): AVX2's algorithm on 16-lane AVX-512 vectors, offered when CPUID and XGETBV
  report AVX-512 F/BW/DQ/VL/VBMI2/VPOPCNTDQ. It gathers brick cells only for lanes near a brick, and neighbour cells
  only for lanes whose neighbour offset is nonzero. Its results equal AVX2's ball for ball (`balls_bench
  --accuracy`). It runs about 1.1x AVX2 on a Ryzen 7 7700: Zen 4 executes 512-bit operations on 256-bit units, and
  a profile of the step shows no single hotspot that wider masks remove. A rewrite with opmask predicates
  throughout and a scalar path for sparse lanes ran at 0.7x and diverged on the launched level; the accuracy test
  now fails on balls left inside bricks, which caught it.
- **ROCm** (`backends/rocm`, Linux): AMD GPUs through ROCm's own runtime, without HIP. The plugin loads
  `libhsa-runtime64.so.1` at run time, keeps the balls in the agent's VRAM pool, and runs each step as AQL dispatch
  packets with barrier bits and one completion signal. The kernels are OpenCL C, compiled at build time by ROCm
  clang to gfx1030/gfx1031 code objects that the plugin embeds and loads with `hsa_executable_*`. HSA's agent UUID
  is not Vulkan's, so `DeviceInventory` also merges GPUs at the same PCI location: the RX 6700 XT is one device
  offering ROCm and Vulkan.
- **VulkanMulti** (`backends/vulkan/VulkanMultiBackend`): one Vulkan backend over several GPUs, picked as
  `VulkanMulti@gpu0|gpu1` (bare `VulkanMulti`: every discrete GPU). It is a `DeviceComposer` whose parts are
  `VulkanBackend`s on one `VkInstance`, driven from one thread: `beginStep` records and submits each device's step,
  then `finishStep` waits for each. It ties the two-part Vulkan Hybrid; doing without the helper threads saves
  about what the single thread's serial readback costs.

Composer changes that came with them:

- A part is parked when the composed step stays slower than the other parts would be without it (before: than the
  lead part alone). With three parts the old rule kept a CPU that held two GPUs back: Zen4 + CUDA + ROCm stepped at
  17.2 G at 96M balls instead of 32.3 G.
- VulkanMulti times its devices by kernel time, so it parks against the slowest kernel rather than the step's
  wall time; otherwise the host readback alone could park the RX 6700 XT.
- A composer that is itself a part (VulkanMulti inside Hybrid) restarts its parking comparison when the outer
  composer moves balls in or out.

### Results on the two-GPU desktop

Same machine and method as above (`balls_bench --grid`, 1500 ticks, averages include the first ticks while shares
settle; the two-GPU desktop numbers the RX 6700 XT `gpu0` and the RTX 2060 `gpu1`).

| Backend | 32M balls | 96M balls |
|---|---|---|
| AVX2 | 0.96 G | 0.99 G |
| Zen4 | 1.06 G | 1.08 G |
| CUDA (RTX 2060) | 18.1 G | 19.1 G |
| Vulkan (RX 6700 XT) | 15.3 G | 15.4 G |
| ROCm (RX 6700 XT) | 17.4 G | 17.4 G |
| CUDA+AVX2 | 15.8 G | 16.4 G |
| Hybrid Zen4 + CUDA | 17.2 G (CPU parked) | 18.2 G (CPU parked) |
| Hybrid Vulkan (RX) + Vulkan (RTX) | 26.0 G | 28.2 G |
| VulkanMulti (RX + RTX) | 26.3 G | 28.2 G |
| Hybrid CUDA + Vulkan (RX) | 28.0 G | 31.2 G |
| **Hybrid CUDA + ROCm** | **29.6 G** | **33.1 G** |
| Hybrid Zen4 + CUDA + ROCm | 30.0 G (CPU parked) | 32.3 G (CPU parked) |
| Hybrid Zen4 + VulkanMulti | 22.3 G (CPU parked) | 22.5 G (CPU parked) |

- ROCm beats Vulkan on the same RX by 13-14%, and CUDA + ROCm splits the balls about 50/50 where CUDA + Vulkan
  settles at 57/43. A full 32M-ball game (`--full`) ends with the same balls and bricks on both (31,999,322 and
  336), at 2.12 ms per tick for ROCm and 2.42 ms for Vulkan.
- The CPU is parked in every set with a GPU: a Ryzen 7 7700 steps about a seventeenth of what one of these GPUs
  does.
- Zen4 + VulkanMulti stays about 10-20% below VulkanMulti alone even over 6000 ticks, with the CPU parked and both
  GPUs stepping; the cause is not pinned down (the nested composer's step adds 0.2-0.3 ms over VulkanMulti's).
  Picking the GPUs as separate parts (`Zen4@cpu+Vulkan@gpu0+Vulkan@gpu1`) runs at the two-GPU speed (24.5 G at
  32M in the same session).
- Run-to-run noise on the two-GPU desktop is about 5-8%; VulkanMulti and the two-part Vulkan Hybrid trade places between runs.
