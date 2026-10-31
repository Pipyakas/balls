# Level format

Each level is a pair of files in `data/assets/levels/normal` or `data/assets/levels/performance`, in the format these sample levels use:

- `<Name>_Bricks.txt`
  - `[Board] Width / Height / PlayableHeight`: brick rows, plus total rows including the empty play space.
    Without `PlayableHeight`, or with one below `Height`, the board gets a quarter of its brick rows as open space
    below them, at least 7 rows.
  - `[Layout]`: a grid of brick ids. Cells that share an id form one multi-cell brick. `-1` marks an empty cell.
  - `[BrickTypes]`: one `id Color hits` line per brick.
- `<Name>_SpawnBall.txt`
  - `[Board] Width` (launcher columns) and `Height` (visible rows).
  - `[Layout]`: `Color(count)` or `Empty` per slot.

Colors: Red, Orange, Yellow, Green, Blue, Indigo, Violet.

### Maps that stray from the format

The parser repairs what it can instead of refusing the level. A well-formed file never reaches these repairs, so it
loads exactly as written. A repaired level still loads, with a warning toast naming the file and the first repair when
it starts, and the same warning when it is imported.

- A UTF-8 byte order mark, `#` and `//` comment lines, and section, key and color names in any case.
- `Key: value` and `Key=value` in `[Board]`; lines there that are not a key and a number are ignored.
- Brick layout rows that disagree with `Width`/`Height`: the rows are taken as written, as wide as the longest, and
  shorter rows are padded with empty cells. A missing `Height` is the row count.
- `Empty`, `.`, `-`, `_` and `x` as empty cells.
- `[BrickTypes]` lines with an unknown color, hits of 0 or less or too few fields are skipped, as is a repeated id
  after its first line; their cells stay empty. Extra fields after the hits are ignored. No `[BrickTypes]` at all
  gives an empty board.
- Launcher rows of different lengths are padded with `Empty`; a missing `Width` is the longest row, a missing
  `Height` shows up to 3 rows, and the launcher file may leave `[Board]` out. `Red (10)` with a space is read as `Red(10)`, and a launcher of 0 balls is `Empty`.

Still refused, with the file and the reason: no `[Board]`, `[Layout]` or brick `Width`, a cell that is not a number or
an empty mark, a launcher that is not `Color(count)` or names an unknown color, and boards past the size limit.

`data/assets/levels/Gameplay.txt` holds the rules every level plays by, in the same `[Section]` / `Key Value` text:
ball radius and speed, how many bounces before a ball heads for the nearest brick of its color, the launch angle,
the physics tick rate and the Normal-mode spawn rate. Values the simulation cannot run (a ball that would cross more
than its radius in a tick, say) are refused with the level's load error, and a key left out keeps its built-in value.

`performance/Unlimited_921Bricks` has the `DA_1000Bricks` board with one launcher per color at the largest count the format
takes (2,147,483,647), so even at x1 it holds more balls than any machine's capacity: auto release keeps spawning
until the ball limit stops it.

## Shipped levels

| Mode | Level | What it shows |
|---|---|---|
| Normal | `DA_FirstLevel`, `DA_49Bricks` | The sample levels |
| Normal | `Warmup_24Bricks` | 3 colors: single and 2-hit bricks, two 2-wide and two 2-tall bricks |
| Normal | `Pillars_28Bricks` | 4 colors: 3-wide and 2-tall multi-hit bricks, open cells at the bottom of two columns |
| Normal | `Stalactites_33Bricks` | 5 colors: columns of different lengths, so each column slides on its own |
| Normal | `Fortress_43Bricks` | 7 colors: 2x2 bricks of 4 hits, 3-wide bricks of 3 hits and a ragged bottom row |
| Performance | `DA_1000Bricks` | The sample level |
| Performance | `Unlimited_921Bricks` | `DA_1000Bricks`' board with launchers that never run out |
| Performance | `Stress_Solid_128x128_16384Bricks` | 16,384 single-hit bricks: the column slide under constant breaking |
| Performance | `Stress_Tough_256x256_65536Bricks` | 65,536 bricks of 100,000 hits: brick-hit resolution on a full board |
| Performance | `Stress_Checker_512x512_65536Bricks` | 65,536 tough bricks with a gap on every side: hits and bounces everywhere |
| Performance | `Stress_Shelves_512x512_57344Bricks` | Long rows of tough bricks with doors: balls cross the board between hits |
| Performance | `Stress_Wide_2048x64_32768Bricks` | A board 2,048 cells wide and 64 deep: long bounces across a very wide grid |
| Performance | `Stress_LoneBrick_512x512_1Brick` | One brick on an empty 512x512 board: free flight, the most balls at 60 FPS |
| Performance | `Billion_7Bricks` | Maps from outside the team: one row of 7 bricks, 7 columns wide, under a billion balls |
| Performance | `Why_11162Bricks`, `Feelgoodman_11357Bricks`, `IDontFeelSoGoodNow_11727Bricks` | 128x128 image maps of single bricks, with thousands of launchers (`PlayableHeight 160` added to the first two, which left it out) |
| Performance | `Milo_2779Bricks` | A 64x64 image map |

Every level in Normal mode releases exactly as many balls of a color as its bricks of that color take hits. The
Performance stress levels launch every color from launchers of 2,147,483,647 balls, so release stops only at the
ball limit. `scripts/levels/GenerateStressLevels.py` writes them.
