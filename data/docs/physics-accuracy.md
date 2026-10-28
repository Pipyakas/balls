# Physics accuracy: optimized backends against a naive reference

Every speed-up is checked against `ReferenceBackend` (`source/core/sim/ReferenceBackend.cpp`), so a faster backend
can never come from quietly changing the physics. The reference shares no stepping code with the optimized
backends: it uses double precision, splits every tick into sub-steps of at most 0.01 cells so contacts are found
where they happen, and writes the game rules out plainly. It runs through the same `Simulation` as the others, so
spawns, the board, hit rules and settling are identical; only ball stepping differs.

## How to run it

- `balls_tests` gates every build: in an empty box, with and without the visibility grid (free flight and
  stepping), every available backend must stay within 0.02 cells mean and 0.06 cells worst of the reference after
  600 ticks, keep every ball inside the field, and keep its speed.
- `balls_bench --dir data/assets/levels/performance --accuracy [--backend a,b]` runs the larger comparison and
  prints, per backend: the worst gaps in bricks left, balls left and total hits over the run, invariant violations
  (balls outside the field, balls resting inside a live brick, speed error), the per-ball position error in the
  empty box, and the hits ahead of or behind the reference at each sample.
- The `Ref/2` row is the reference against itself at half the sub-step. Brick hits are chaotic, so two correct
  engines drift apart too; that row is the noise floor the curve gaps are read against.

## Results (2026-10-28, 20,000 balls, 1,200 ticks)

Empty box, mean / worst distance from the reference after 10 s, in cells:

| Backend | Free flight | Stepped |
|---|---|---|
| Scalar | 0.0004 / 0.002 | 0.0004 / 0.002 |
| AVX2 | 0.009 / 0.025 | 0.011 / 0.040 |
| CUDA, Hybrid | 0.009 / 0.025 | 0.009 / 0.025 |
| Vulkan | 0.011 / 0.039 | 0.011 / 0.039 |

The compact backends store a direction in 13 bits per axis, so a ball's heading is off by up to about 1/8000 of a
radian; over 10 s of flight that is the hundredth of a cell above.

DA_1000Bricks with balls launched as in a game: no backend ever put a ball outside the field or left one inside a
live brick, and speed never changed. Total hits end equal and run 10-35 hits off the reference during the first
four seconds, because the game checks contacts once per tick (0.117 cells of travel) where the reference checks every
0.01 cells; the whole launch reaches the bricks together, so a few balls landing a tick earlier or later move the
early curve. Bricks left differ by up to about 20 near the end, which reads as 22-46% when only ~80 remain. How large
these look depends on the launch's random draw: with the original sequential generator they were 3-9% and 10-20
hits, with the per-ball hashed launch (parallel generation) 22-46% and 10-35 hits, Scalar included, while the board
clears on the same frame with the same total.

## What it has caught

- Stepped balls drifted 0.12 cells in 10 s (0.2% speed error per axis): every frame a stepped position was rounded
  to nearest after moving a fixed ~255.4 units, dropping the same fraction each time. Positions are now rounded
  with a hash-based dither (`roundingDither`), which is unbiased: 0.011 cells.
- Before the line-of-sight rule, retargeted balls looped forever on bricks hidden behind others (see the retarget
  commit); the reference implements the rule independently.
