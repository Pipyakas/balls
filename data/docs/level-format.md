# Level format

Each level is a pair of files in `data/assets/levels/normal` or `data/assets/levels/performance`, in the format these sample levels use:

- `<Name>_Bricks.txt`
  - `[Board] Width / Height / PlayableHeight`: brick rows, plus total rows including the empty play space.
  - `[Layout]`: a grid of brick ids. Cells that share an id form one multi-cell brick. `-1` marks an empty cell.
  - `[BrickTypes]`: one `id Color hits` line per brick.
- `<Name>_SpawnBall.txt`
  - `[Board] Width` (launcher columns) and `Height` (visible rows).
  - `[Layout]`: `Color(count)` or `Empty` per slot.

Colors: Red, Orange, Yellow, Green, Blue, Indigo, Violet.

`data/assets/levels/Gameplay.txt` holds the rules every level plays by, in the same `[Section]` / `Key Value` text:
ball radius and speed, how many bounces before a ball heads for the nearest brick of its color, the launch angle,
the physics tick rate and the Normal-mode spawn rate. Values the simulation cannot run (a ball that would cross more
than its radius in a tick, say) are refused with the level's load error, and a key left out keeps its built-in value.

`performance/Unlimited` has the `DA_1000Bricks` board with one launcher per color at the largest count the format
takes (2,147,483,647), so even at x1 it holds more balls than any machine's capacity: auto release keeps spawning
until the ball limit stops it.
