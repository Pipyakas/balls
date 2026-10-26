# Hybrid CPU + GPU physics

Balls never collide with each other, so the ball set can be split between devices. The `Hybrid` backend
(F4, `/backend?kind=hybrid`) keeps most balls on CUDA and a share on the AVX2 backend, steps both at the same
time, and merges their results on the host. Measured on a GTX 1650 with an i5-12400F (6 cores, 12 threads,
dual-channel DDR4-3200).

## How it works

- `HybridBackend` (`core/sim/`) owns a CUDA and an AVX2 backend. Each step runs the AVX2 step on a helper
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

## What made the CPU side worth adding

The first hybrid was slower than CUDA alone. Three changes fixed it:

| Change | AVX2 throughput (2 substeps) |
|---|---|
| Original: seven float/int arrays, 28 bytes read and 24 written per ball | 650 M ball-steps/s |
| Compact 8-byte ball (CUDA's encoding), state written only when it changes, near-brick skip | 1080 M (bricks), 1250 M (empty board) |
| Sampled visibility-grid bids (8 per slot) instead of every ball writing the shared grid | in-game 610 M -> 1200 M |

The last one matters only in the game: eleven threads storing every ball into the same ~45 KB grid made its
cache lines bounce between cores and halved CPU throughput.

## Results

`balls_bench --scatter` (full `Simulation`, scattered balls, empty board after the first ticks):

| Balls | CUDA | Hybrid | Gain |
|---|---|---|---|
| 96M, 3000 ticks, 2 substeps | 10.29 ms/tick | 9.58 ms/tick | 7% (9.2 ms steady, 11%) |

In the game, hidden window, 140M balls, three alternating runs each:

| Backend | FPS | Frame |
|---|---|---|
| CUDA | 67.4 | 14.83 ms |
| Hybrid | 68.7 | 14.63 ms |

In-game the gain shrinks to about 2%, within run-to-run noise, for two reasons:

- At 60-70 FPS with 120 Hz ticks, frames alternate between one and two substeps. The CPU's cost scales with
  substeps (about 7 vs 13 ms for its share) while the memory-bound GPU's barely does (11.2 vs 13.4 ms), so no
  single split balances both kinds of frame.
- Each frame waits for the slower side. Averaged separately, both sides take about 13.8 ms, but the
  per-frame maximum averages about 15 ms.

The CPU can only ever add its own throughput: about 1.2 G ball-steps/s against the 1650's 9-10 G. On a CPU with
more cores and faster memory the share grows, but the GPU still carries most of the balls.
