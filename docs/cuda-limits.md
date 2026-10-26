# CUDA step kernel: hardware limits on the GTX 1650

Measured on the dev machine (GTX 1650, 14 SMs of Turing TU117, 128-bit GDDR6 at 6001 MHz). The kernel ran at
**1920 MHz** under load (`nvidia-smi -q -d CLOCK,POWER,PERFORMANCE`) with no power or thermal capping.

Hardware ceilings:

| resource | ceiling | how it was measured |
|---|---|---|
| DRAM bandwidth | 192 GB/s theoretical, **165.9 GB/s** device-to-device copy | `balls_bench --roofline` |
| instruction issue | 14 SMs x 4 schedulers x 1.92 GHz = **107.5 G warp-instructions/s** | Turing issues one warp-instruction per scheduler per clock |

## Empty board: memory-bound

`balls_bench --roofline --balls 60000000` streams 16 bytes per ball per tick (an 8-byte ball read and written once
for two fused substeps). The kernel reaches **153.8 GB/s = 93.7% of the measured copy bandwidth** (137-154 GB/s
across runs). The DRAM is the limit, and the GPU cannot move the balls faster.

## Brick level: instruction-issue-bound

On `DA_1000Bricks` the kernel takes 12.2-13.2 ms for 60M balls, or 44-49% of copy bandwidth. Memory isn't the
limit there. Skipping the near-brick path drops the kernel to 6.2 ms (93.7% of copy), so the brick-contact code
costs the extra 6 ms.

GPU performance counters need administrator rights on Windows (CUPTI stops reporting once the CUDA context is
created), so the issue limit is measured with an **instruction-injection test** (`tools/bench/issue_slack.py`)
instead. It adds K independent, non-foldable ALU instructions per thread (alternating IMAD and PRMT, so both math
pipes stay at one issue per clock) and compares the extra kernel time with what K full issue slots cost:
`K x warps / 107.5 G/s` = 0.0174 ms per instruction for 1.875M warps.

- If the schedulers have idle cycles (the kernel waits on memory or latency), the injected instructions fill them
  and cost less than a full slot.
- If every issue cycle is already used, each injected instruction costs a full slot.

| board | K = 32 | K = 64 | K = 128 |
|---|---|---|---|
| brick level | 144% | 92% | 93% |
| empty board | 59% | 72% | 74% |

The brick level has no spare issue cycles: every added instruction costs a full issue slot from the first one, so
the SMs' instruction issue rate is the limit. The empty board, which is memory-bound, shows the expected slack,
which confirms the test can tell the two cases apart.

```powershell
python tools/bench/issue_slack.py --on-level --sm-clock-mhz 1920 --ticks 400
python tools/bench/issue_slack.py --sm-clock-mhz 1920
```

## Spatial sorting

Only 19% of ball-substeps are in a near-brick cell and 2.5% touch a brick. Yet with balls in arbitrary order almost
every 32-lane warp contains one, so the near path ran at about 19% lane utilization. That was wasted issue, not a
hardware limit. The backend now sorts balls by board cell on the GPU:

- a block-aggregated counting pass (`countCellsKernel`, warp-aggregated shared atomics through `__match_any_sync`)
  and a single-block scan give each cell its output range;
- on a sort tick, `stepAndSortBallsKernel` steps 4096 balls per block and writes each one straight to its
  cell-sorted slot in a second buffer, so sorting costs only the 4-byte counting pass instead of a separate
  16-byte scatter pass.

Coherence lasts a while because balls move about 0.23 cells per tick:

| sort interval (400 ticks, brick level, 60M balls) | kernel |
|---|---|
| never | 15.5 ms |
| every tick | 14.6 ms |
| every 4 ticks | 10.0 ms |
| every 8 ticks | **9.76 ms** |
| every 16 ticks | 9.85 ms |
| every 32 ticks | 10.8 ms |

Sorting only costs time on an empty board, so the backend picks the interval itself. It trials {never, 8, 16} for
32 measured ticks each, keeps the cheapest GPU time per ball, and re-trials every 1200 ticks. Over 400 ticks
(trials included) the brick level drops from 16.8 ms to 10.2 ms (-39%), and the empty board stays at memory speed
(7.0 ms incl. trials vs 6.5 ms). The in-game capacity probe at 60 FPS went from 130.0M to 140.9M balls.

With sorting active the brick level uses about 80% of issue slots (marginal injection cost 79-85%) and about 63%
of copy bandwidth. The remaining loss is divergence inside near-brick warps, between balls at a cell edge with an
occupied neighbor and balls in the middle of the cell. Sorting by edge band removes it only for about a tick,
because balls cross a band nearly every tick (see below), so that divergence is a property of the motion, not of
the memory layout.

## What did not help

Each of these was measured on the brick level and reverted:

| change | result |
|---|---|
| neighbor-occupancy masks to skip the contact loop | 13.7 ms (no gain) |
| streaming / read-only cache hints | 13.6 ms (no gain) |
| edge-distance fast path for contact candidates | -48 static instructions, no measurable gain |
| block-level compaction of near-brick balls through shared memory | 16.9 ms (per-substep barriers cost more than the saved lanes) |
| sorting inside 256-thread step blocks | 11.8 ms (sorted writes scatter: 0.2 balls per cell per block) |
| separate scatter pass instead of the fused sort | every tick 17.4 ms, every 4 ticks 10.7 ms (fused: 14.6 / 10.0) |
| sort key (cell, 3x3 edge band) instead of cell | 9.68 vs 10.36 ms when fresh (-6.5%), but balls cross a 0.2-cell band about every tick (0.23 cells/tick), so the order decays before a sort pays for itself |
