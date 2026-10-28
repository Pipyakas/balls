# Performance edge cases

The scoring map is unknown, so the backends are measured on generated boards that each stress one thing, from
16x16 cells up to 2048x2048, and the game is made to slow down gradually rather than stall on any of them.

## The boards

`source/core/level/EdgeCaseLevels.cpp` generates ten layouts at seven sizes (16x16, 48x48, 128x128, 512x512,
2048x2048, and 2048x64 / 64x2048 strips). Bricks fill the top `size` rows; below them is a launch gap of a quarter of
the height. "Tough" bricks have a billion hits, so the board never clears and free flight never starts.

| Layout | Bricks | What it stresses |
|---|---|---|
| Empty | none | free flight from the first frame: memory and drawing |
| Solid | every cell, 1 hit, random colours | a dense board that clears, then free flight |
| Tough | every cell, a billion hits | contacts and kills along one face that never breaks |
| Checker | every other cell in both directions, tough | a lattice of pillars: contacts everywhere |
| Columns | walls every 4 columns with doors, tough | long corridors |
| Shelves | rows every 4 with gaps, tough | balls trapped between shelves |
| Sparse | 5% of cells at random, tough | scattered contacts across the whole board |
| LoneBrick | one tough brick | no free flight, almost no contacts |
| Monochrome | every cell red, 1 hit | only a seventh of the balls can break bricks |
| Cage | a tough box around the launch point | every ball trapped in a few cells |

The 16, 48 and 128 boards ship as Performance levels (`data/assets/levels/performance/Edge_*`), written by
`balls_bench --write-edge-levels`. Level files keep the rule that launchers hold enough balls to clear the board, so
their tough bricks get as many hits as 1.5 billion per colour allows.

## How a case is measured

`balls_bench --edge-cases [--sizes 48x48,...] [--layouts Tough,...] [--backend ...] [--case-seconds N] [--out t.tsv]`,
run on the machine being measured. Each case
(`source/core/sim/EdgeCaseSweep.cpp`):

1. Sizes the visibility grid as the game does on a 1920x1080 screen (at most one slot per pixel).
2. Fills the board through `SpawnPacer`, as the game does, until a 30-frame window of physics (two ticks a frame)
   averages more than 80% of a 60 FPS frame, the backend is full, or the case's time runs out.
3. Settles for one and a half crossings of the board's height (at most 20 s), launching only to replace balls that
   died, since the last launches are still flying and cost less than balls working along the bricks.
4. If the settled frame is under 85% of the budget, goes back to filling (launching costs a frame more than the
   balls it adds will), settling 3 s between rounds.

The result is the settled window's live count, scaled down if its frames no longer fit the budget, with the worst
single frame seen. `--trace-frames N --balls N` (or `--trace-spawn-to N`) prints every frame of one case instead.

## Results on the target PC

i9-14900K, 64 GB, GTX 1650. Balls held at 60 FPS; "(mem)" means the backend was full (memory, not time, was the
limit).

| Board | AVX2 | CUDA | Hybrid | Best | Worst frame (best) |
|---|---|---|---|---|---|
| Empty 16x16 | 7B (mem) | 403M (mem) | 7.4B (mem) | Hybrid | 18 ms |
| Solid 16x16 | 7B (mem) | 403M (mem) | 7.4B (mem) | Hybrid | 18 ms |
| Tough 16x16 | 14M | 69M | 65M | CUDA | 30 ms |
| Checker 16x16 | 13M | 59M | 56M | CUDA | 20 ms |
| Columns 16x16 | 15M | 66M | 64M | CUDA | 22 ms |
| Shelves 16x16 | 16M | 78M | 73M | CUDA | 30 ms |
| Sparse 16x16 | 15M | 74M | 69M | CUDA | 23 ms |
| LoneBrick 16x16 | 46M | 120M | 161M | Hybrid | 25 ms |
| Monochrome 16x16 | 7B (mem) | 403M (mem) | 7.4B (mem) | Hybrid | 17 ms |
| Cage 16x16 | 14M | 66M | 62M | CUDA | 32 ms |
| Empty 48x48 | 7B (mem) | 403M (mem) | 7.4B (mem) | Hybrid | 10 ms |
| Solid 48x48 | 7B (mem) | 403M (mem) | 7.4B (mem) | Hybrid | 11 ms |
| Tough 48x48 | 24M | 95M | 105M | Hybrid | 31 ms |
| Checker 48x48 | 18M | 74M | 80M | Hybrid | 23 ms |
| Columns 48x48 | 16M | 77M | 78M | Hybrid | 24 ms |
| Shelves 48x48 | 24M | 94M | 95M | Hybrid | 28 ms |
| Sparse 48x48 | 18M | 86M | 85M | CUDA | 26 ms |
| LoneBrick 48x48 | 67M | 122M | 176M | Hybrid | 29 ms |
| Monochrome 48x48 | 7B (mem) | 403M (mem) | 7.4B (mem) | Hybrid | 10 ms |
| Cage 48x48 | 15M | 70M | 65M | CUDA | 27 ms |
| Empty 128x128 | 7B (mem) | 386M (mem) | 7.3B (mem) | Hybrid | 14 ms |
| Solid 128x128 | 7B (mem) | 386M (mem) | 7.3B (mem) | Hybrid | 11 ms |
| Tough 128x128 | 42M | 99M | 121M | Hybrid | 35 ms |
| Checker 128x128 | 25M | 89M | 100M | Hybrid | 27 ms |
| Columns 128x128 | 17M | 80M | 85M | Hybrid | 25 ms |
| Shelves 128x128 | 36M | 99M | 114M | Hybrid | 36 ms |
| Sparse 128x128 | 19M | 85M | 93M | Hybrid | 30 ms |
| LoneBrick 128x128 | 73M | 114M | 175M | Hybrid | 32 ms |
| Monochrome 128x128 | 7B (mem) | 386M (mem) | 7.4B (mem) | Hybrid | 13 ms |
| Cage 128x128 | 14M | 64M | 77M | Hybrid | 28 ms |
| Empty 512x512 | 26M | 205M | 6.9B | Hybrid | 18 ms |
| Solid 512x512 | 16M | 103M | 7.4B | Hybrid | 1011 ms |
| Tough 512x512 | 25M | 96M | 126M | Hybrid | 26 ms |
| Checker 512x512 | 31M | 98M | 135M | Hybrid | 35 ms |
| Columns 512x512 | 19M | 108M | 106M | CUDA | 39 ms |
| Shelves 512x512 | 24M | 98M | 126M | Hybrid | 27 ms |
| Sparse 512x512 | 15M | 92M | 113M | Hybrid | 30 ms |
| LoneBrick 512x512 | 31M | 108M | 182M | Hybrid | 32 ms |
| Monochrome 512x512 | 199M | 369M (mem) | 7.4B | Hybrid | 26 ms |
| Cage 512x512 | 3M | 46M | 55M | Hybrid | 25 ms |
| Empty 2048x2048 | 6M | 352M (mem) | 1.8B | Hybrid | 18 ms |
| Solid 2048x2048 | 5M | 108M | 142M | Hybrid | 56 ms |
| Tough 2048x2048 | 8M | 121M | 174M | Hybrid | 37 ms |
| Checker 2048x2048 | 8M | 110M | 165M | Hybrid | 34 ms |
| Columns 2048x2048 | 6M | 118M | 155M | Hybrid | 33 ms |
| Shelves 2048x2048 | 9M | 114M | 157M | Hybrid | 27 ms |
| Sparse 2048x2048 | 8M | 112M | 145M | Hybrid | 30 ms |
| LoneBrick 2048x2048 | 5M | 125M | 174M | Hybrid | 34 ms |
| Monochrome 2048x2048 | 5M | 107M | 117M | Hybrid | 35 ms |
| Cage 2048x2048 | 2M | 48M | 56M | Hybrid | 27 ms |
| Empty 2048x64 | 6.8B (mem) | 386M (mem) | 7.5B (mem) | Hybrid | 7 ms |
| Solid 2048x64 | 28M | 104M | 128M | Hybrid | 31 ms |
| Tough 2048x64 | 26M | 88M | 106M | Hybrid | 28 ms |
| Checker 2048x64 | 18M | 77M | 85M | Hybrid | 37 ms |
| Columns 2048x64 | 15M | 71M | 75M | Hybrid | 32 ms |
| Shelves 2048x64 | 25M | 87M | 94M | Hybrid | 27 ms |
| Sparse 2048x64 | 17M | 80M | 84M | Hybrid | 28 ms |
| LoneBrick 2048x64 | 65M | 118M | 174M | Hybrid | 32 ms |
| Monochrome 2048x64 | 29M | 115M | 136M | Hybrid | 30 ms |
| Cage 2048x64 | 7M | 39M | 46M | Hybrid | 25 ms |
| Empty 64x2048 | 7B (mem) | 386M (mem) | 7.5B (mem) | Hybrid | 10 ms |
| Solid 64x2048 | 86M | 61M | 100M | Hybrid | 46 ms |
| Tough 64x2048 | 91M | 89M | 168M | Hybrid | 29 ms |
| Checker 64x2048 | 86M | 95M | 167M | Hybrid | 36 ms |
| Columns 64x2048 | 86M | 97M | 167M | Hybrid | 29 ms |
| Shelves 64x2048 | 91M | 98M | 172M | Hybrid | 30 ms |
| Sparse 64x2048 | 87M | 101M | 174M | Hybrid | 35 ms |
| LoneBrick 64x2048 | 93M | 43M | 99M | Hybrid | 47 ms |
| Monochrome 64x2048 | 88M | 95M | 164M | Hybrid | 38 ms |
| Cage 64x2048 | 16M | 45M | 48M | Hybrid | 31 ms |

## What the cases found

- **Bricks with huge hit counts crashed the GPU backends.** CUDA sized its per-step kill buffer by the board's total
  hits (terabytes for tough bricks) and Vulkan refused the board. The buffer now stops at 4M kills; an overflowing
  kill is refunded and lands a later step.
- **Kill waves stalled the GPU.** A launch wave hitting one row of bricks made hundreds of thousands of balls take hits
  from the same few bricks with compare-and-swap loops: 150 ms steps. Hits are now taken by subtract-and-refund,
  shared per warp: 1 ms.
- **Spawning froze the game.** Launchers, the probe and fills released up to billions of balls in one frame. All of
  them now go through `SpawnPacer` (below).
- **Launching was single-threaded** at 33-93 ns a ball. It now runs on worker pools that stay up, 3-10 ns a ball.
- **Growing the CUDA arrays copied every ball**, through host memory near the top of device memory: 450 ms frames.
  They now grow in place with CUDA virtual memory, and launches upload asynchronously.
- **Filling the CPU store near the memory ceiling** held a frame up to a second while Windows trimmed working sets.
  The next 64 MB is now faulted in on a background thread.
- **The sort schedule** trialled not sorting and then re-sorted a scattered array in one 60-70 ms step. It now always
  sorts every 8-32 ticks, from the first tick.
- **Hybrid's CPU side drew a full share of the picture** however few balls it held (15-17 ms a frame on a 128 board);
  it now draws in proportion to its balls. The AVX2 free-flight drawing also runs eight balls at a time.
- **The probe misjudged an uncapped game.** With hundreds of frames a second and a tick in only some of them, the
  average frame said 426 FPS on a board using 79% of each second. The probe now fits frame time against ticks run and
  judges what a 60 FPS frame with its two ticks would cost; it also stops once balls die as fast as it adds them.

## Spawn pacing

`source/core/game/SpawnPacer.cpp` decides how many balls a frame may add, for launchers, the probe and fills alike:

- making them (generating and encoding) may take a quarter of a 60 FPS frame, from the measured cost per ball;
- the balls on the board may at most double per second, on top of replacing the ones that died, so frame time
  rises gradually and boards whose bricks absorb balls as fast as they arrive still fill.

The probe's and fill's additions queue behind it, and the probe measures only once its queue has drained.
`scripts/bench/stutter_check.py` runs the game hidden and reports each second's average and worst frame while a
board fills; a frame over twice a 60 FPS frame counts as a stutter.

Hybrid is the best backend or within a few percent of it on every board; CUDA leads by 2-7% on the 16x16 brick
boards and on 512x512 Columns. Before the sparse hit and target uploads, Hybrid held only 11-16M on the 2048x2048
brick boards.

## Known gaps

- One 1 s frame was seen filling Hybrid to 7.4B on the solid 512x512 board, as the CPU store reached the memory line;
  the other memory-bound fills stayed under 20 ms.

- Vulkan still copies its buffers to grow them (150-580 ms frames) and has no free flight; it is the fallback for
  non-NVIDIA GPUs and is not used on the target PC.
- On boards bigger than the screen, AVX2's free-flight drawing samples 8-16 balls per pixel; its cost follows the
  screen, not the ball count, but is the CPU-side limit there.
