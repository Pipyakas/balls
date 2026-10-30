# Free flight: balls that only meet walls are not stepped

In Performance mode the capacity probe adds balls until the frame rate drops. With millions of balls the board is
cleared within the first second, and from then on nothing can change a ball's path except the four walls. Stepping
those balls every tick read and wrote every ball, so the ball count at 60 FPS was set by memory bandwidth
(about 150M on the GTX 1650, 262M with the CUDA+AVX2 split, since replaced by Hybrid, on the i9-14900K).

## The closed form

Between two walls a coordinate is a triangle wave of time. Unfolding the reflections turns it into a straight
line over a period of twice the travel span (`extent - 2 * radius`). The phase along that line is kept as 32-bit
fixed point over one period, so a ball's phase at tick `t` is

    phase(t) = phase(E) + advance * (t - E)    (mod 2^32)

where `E` is an epoch tick and `advance` is the per-tick distance in the same units. The multiplication wraps
exactly, so there is no drift however long the balls fly. Folding the phase back gives the position and the sign
of the velocity; the number of half periods crossed gives the wall bounces. The helpers live in
`source/core/sim/kernels/BallStepCore.h` (`flyFreely`, `flyFreelyBackwards`) and run on the host and in CUDA.

No new ball format was needed: a 16-bit position plus the sign of the direction maps one-to-one to the unfolded
phase, so the compact 8-byte ball stores "this ball at tick E" unchanged.

## How the backends use it

- `Simulation` passes a tick clock and a `freeFlight` flag in `StepParams`: set when no brick is alive and
  ball-vs-ball collisions are off.
- CUDA and AVX2 freeze on the first free-flight step: they record `E` and stop touching the ball arrays.
- A frame only needs the balls that bid for a visibility-grid slot, so whole 256-ball chunks are sampled with the
  bidder stride. The reads stay contiguous and the cost follows the size of the grid, not the ball count:
  about 0.05 ms of GPU time per frame on the GTX 1650 at any count.
- Balls added while frozen (launchers, the probe) are projected back to `E` with the quantized direction the
  kernels decode, so flying forward lands exactly on the spawn point.
- Any normal step, export or transfer between devices first writes every ball back at the current tick
  (`materializeFreeFlightKernel`, `CompactBallStore::materializeRange`), so the rest of each backend never sees
  the epoch form.
- Hybrid stops rebalancing in free flight (no part steps, and moving balls would force a write-back); new balls
  go to each part by its share while it has room, then wherever there is room.

## Memory becomes the limit

Without stepping, the ceiling is how many balls fit:

- `PhysicsBackend::ballCapacityLimit()`: CUDA uses `cuMemGetInfo` minus a 256 MB reserve at 8 bytes per ball;
  AVX2 uses the memory that is free right now minus a margin (1 GB or 1/32 of RAM) at 8 bytes per ball, and
  drops to the current count as soon as Windows signals low memory. The separate 4-byte render copy is only kept
  while every ball has its own visibility slot, so above the grid size a held ball costs 8 bytes, not 12.
- Near the top of device memory CUDA grows its arrays through host memory, since the old and new arrays no longer
  fit side by side. A failed allocation keeps the current capacity instead of faulting the backend.
- `Simulation::scatterBalls` clamps to the room left and hands balls to the backend in 4M-ball batches, and the
  capacity probe ends its ramp when the room runs out.

## Results

Max balls with the probe at 60 FPS on DA_1000Bricks, NVRHI D3D12:

| Machine | Backend | Stepping | Free flight |
|---|---|---|---|
| i9-14900K, 64 GB DDR5, GTX 1650 (target PC) | CUDA | 150M | 287M at ~4,500 FPS (before host-staged growth) |
| i9-14900K, 64 GB DDR5, GTX 1650 (target PC) | CUDA+AVX2 (now Hybrid) | 262M | 4.37 billion at ~2,600 FPS, min 233 |
| i5-12400F, GTX 1650 (busy development PC) | CUDA | 88M | 392M at ~4,500 FPS |

While bricks are alive nothing changes: balls are stepped exactly as before.
