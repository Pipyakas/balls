# CUDA step kernel: hardware limits on the GTX 1650

Measured on the dev machine (GTX 1650, 14 SMs of Turing TU117, 128-bit GDDR6 at 6001 MHz). The kernel ran at
**1920 MHz** under load (`nvidia-smi -q -d CLOCK,POWER,PERFORMANCE`) with no power or thermal capping.

Hardware ceilings:

| resource | ceiling | how it was measured |
|---|---|---|
| DRAM bandwidth | 192 GB/s theoretical, **165.9 GB/s** device-to-device copy | `balls_bench --roofline` |
| instruction issue | 14 SMs x 4 schedulers x 1.92 GHz = **107.5 G warp-instructions/s** | Turing issues one warp-instruction per scheduler per clock |

Summary: the empty board is bound by DRAM bandwidth. The brick level is bound by DRAM bandwidth when the
kernel has the power budget, and by the board power limit under sustained load.

## Empty board: memory-bound

`balls_bench --roofline --balls 60000000` streams 16 bytes per ball per tick (an 8-byte ball read and written once
for two fused substeps). The kernel reaches **153.8 GB/s = 93.7% of the measured copy bandwidth** (137-154 GB/s
across runs). The DRAM is the limit, and the GPU cannot move the balls faster.

## Unsorted brick level: instruction-issue-bound

On `DA_1000Bricks` the kernel takes 12.2-13.2 ms for 60M balls, or 44-49% of copy bandwidth. Memory isn't the
limit there. Skipping the near-brick path drops the kernel to 6.2 ms (93.7% of copy), so the brick-contact code
costs the extra 6 ms.

GPU performance counters need administrator rights on Windows (CUPTI stops reporting once the CUDA context is
created), so the issue limit is measured with an **instruction-injection test** (`scripts/bench/issue_slack.py`)
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
python scripts/bench/issue_slack.py --on-level --sm-clock-mhz 1920 --ticks 400
python scripts/bench/issue_slack.py --sm-clock-mhz 1920
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

## After sorting: what bounds the sorted step

Event timings in a normal back-to-back run, which is power-capped (see the next section). At sort-every-8 (brick level, 60M balls), normal ticks run the step kernel in 8.33 ms, sort
ticks spend 2.16 ms counting plus 13.1 ms in the sorting step. Sort ticks therefore add about 0.9 ms per tick
on average. On normal ticks:

- DRAM is at about 70% of copy bandwidth (16 B per ball in 8.33 ms) and the injection test shows 68-83% marginal
  issue cost, so neither is saturated;
- occupancy is at Turing's maximum of 32 warps per SM, and capping it costs more than proportionally less work
  would: 24 warps 9.60 ms, 16 warps 12.83 ms, 8 warps 23.30 ms (empty board: 6.24 / 7.86 / 10.61 / 19.14 ms);
- more memory-level parallelism barely helps: 2 balls per thread 8.08 ms, 4 balls per thread 8.08 ms, a
  persistent grid-stride kernel with one-ball prefetch 9.46 ms. Two balls per thread is kept (-1.4% bricks,
  -3% empty board);
- a near/far partition instead of a cell sort (13.4 ms, same as random order) shows that the cell sort's win comes
  from lanes reading the same board words, not from branch coherence.

## Brick level: the board power limit

With "Allow access to the GPU performance counters to all users" enabled, Nsight Compute and CUPTI answer what the
event timings could not:

| measurement | result |
|---|---|
| Nsight Compute, a sorted brick-level step in isolation | 6.4 ms, **DRAM 83% of peak** (~93% of measured copy bandwidth), issue slots 71% busy, 31.3 of 32 lanes active: memory-bound |
| same kernel in a normal back-to-back run (CUPTI kernel timeline = CUDA events) | 8.3 ms sorted, 12-14 ms unsorted |
| warp-instructions per unsorted launch, plain run (CUPTI SASS patching) vs Nsight | 826M vs 850M: the same work |
| `nvidia-smi` every 20 ms during a sustained brick-level run | **SW Power Cap active in 100% of busy samples**, 81.6 W average against the 82 W limit (peak 85.5 W), SM clock 1733 MHz average (1635 min) instead of 1920 MHz |
| empty board, same sampling | 43 W, 1920 MHz, no cap |
| compute-heavy control: empty board + 256 injected instructions | 11.24 ms plain vs 9.32 ms isolated (Nsight: 91% issue active) |

In isolation, the brick-level step is bound by DRAM bandwidth, like the empty board. Under sustained load, every
compute-heavy CUDA kernel on this card runs into the **GTX 1650's 75-82 W board power limit**. The driver then
lowers the SM clock until the power fits, so the same instruction stream takes 30-40% longer. How much depends on the
chip's temperature (leakage power): the same build measured 10.0 ms and 14.8 ms for 400 brick-level ticks at
different points of a long session. The memory-bound empty board draws half the budget and never caps.

Both limits are physical: DRAM bandwidth when power is available, board power under sustained compute. On a power-
capped card, fewer instructions per ball mean less energy per ball. That is why the cell sort, which cut a tick from
850M to 403M warp-instructions, also helps under the cap.

```powershell
pwsh scripts/FetchCudaSdk.ps1 -ProfilerTools
build/win32/bin/Release/balls_insncount.exe --cupti .deps/cupti/bin/cupti64_2025.2.1.dll --dir data/assets/levels/performance --mode timeline --balls 60000000 --ticks 40
build/win32/bin/Release/balls_insncount.exe --cupti .deps/cupti/bin/cupti64_2025.2.1.dll --dir data/assets/levels/performance --mode instructions --balls 60000000 --ticks 9
```

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
| board cell words staged in shared memory | 10.67 vs 9.70 ms (slower; empty board 9.5 vs 6.4 ms) |
| persistent grid-stride step kernel with prefetch | 9.46 vs 8.33 ms on normal ticks |
| plain shared atomics instead of warp-aggregated ranking | count 4.0 vs 2.2 ms, sorting step 14.4 vs 13.1 ms |
| sort key (cell, 3x3 edge band) instead of cell | 9.68 vs 10.36 ms when fresh (-6.5%), but balls cross a 0.2-cell band about every tick (0.23 cells/tick), so the order decays before a sort pays for itself |
