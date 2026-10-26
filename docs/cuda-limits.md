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
python tools/bench/issue_slack.py --on-level --sm-clock-mhz 1920
python tools/bench/issue_slack.py --sm-clock-mhz 1920
```

## What did not help

Each of these was measured on the brick level and reverted:

| change | result |
|---|---|
| neighbor-occupancy masks to skip the contact loop | 13.7 ms (no gain) |
| streaming / read-only cache hints | 13.6 ms (no gain) |
| edge-distance fast path for contact candidates | -48 static instructions, no measurable gain |
| block-level compaction of near-brick balls through shared memory | 16.9 ms (barriers cost more than the saved lanes) |
| balls sorted by cell once at upload | 6% faster, decays as balls move |

Only 19% of ball-substeps are in a near-brick cell, and 2.5% touch a brick. Yet almost every 32-lane warp contains
one, so the near path runs at about 19% lane utilization. Further gains need fewer instructions per ball, for
example periodic spatial sorting so warps are coherent, not more hardware.
