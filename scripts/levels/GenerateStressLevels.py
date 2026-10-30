"""Write the Performance-mode stress levels into data/assets/levels/performance.

Each level stresses one part of the physics at a size the game still runs at 60 FPS on the target PC: brick-hit
resolution (Tough, Checker), wall bounces against long rows (Shelves, Wide), the column slide (Solid) and free flight
with nothing to hit (LoneBrick). Every level uses all seven colors with one launcher per color holding the largest
count the format takes, so release only stops at the ball limit. The output is deterministic.
"""

import random
from pathlib import Path

COLORS = ["Red", "Orange", "Yellow", "Green", "Blue", "Indigo", "Violet"]
REPO = Path(__file__).resolve().parents[2]
OUTPUT_DIRECTORY = REPO / "data" / "assets" / "levels" / "performance"
SEED = 2026
LARGEST_COUNT = 2_147_483_647
TOUGH_HITS = 100_000
LONE_BRICK_HITS = 1_000_000_000
MINIMUM_LAUNCH_ROWS = 4
LAUNCH_ROWS_DIVISOR = 4
SHELF_SPACING = 4
DOOR_SPACING = 8
EMPTY_CELL = "-1"


def solid(column, row, width):
    return True


def checker(column, row, width):
    return column % 2 == 0 and row % 2 == 0


def shelves(column, row, width):
    return row % SHELF_SPACING == 0 and column % DOOR_SPACING != DOOR_SPACING - 1


def lone_brick(column, row, width):
    return column == width // 2


# name, width, brick rows, cell rule, hits per brick, rows of open space below the bricks (None: a quarter of the rows)
LEVELS = [
    ("Stress_Solid_128x128", 128, 128, solid, 1, None),
    ("Stress_Tough_256x256", 256, 256, solid, TOUGH_HITS, None),
    ("Stress_Checker_512x512", 512, 512, checker, TOUGH_HITS, None),
    ("Stress_Shelves_512x512", 512, 512, shelves, TOUGH_HITS, None),
    ("Stress_Wide_2048x64", 2048, 64, checker, TOUGH_HITS, None),
    ("Stress_LoneBrick_512x512", 512, 1, lone_brick, LONE_BRICK_HITS, 511),
]


def write_level(name, width, height, is_brick, hits, open_rows, rng):
    ids = []
    types = []
    for row in range(height):
        line = []
        for column in range(width):
            if is_brick(column, row, width):
                line.append(str(len(types)))
                types.append(f"{len(types)} {rng.choice(COLORS)} {hits}")
            else:
                line.append(EMPTY_CELL)
        ids.append(" ".join(line))
    if open_rows is None:
        open_rows = max(MINIMUM_LAUNCH_ROWS, height // LAUNCH_ROWS_DIVISOR)
    bricks = ["[Board]", f"Width {width}", f"Height {height}", f"PlayableHeight {height + open_rows}", "",
              "[Layout]", *ids, "", "[BrickTypes]", *types]
    (OUTPUT_DIRECTORY / f"{name}_Bricks.txt").write_text("\n".join(bricks) + "\n", newline="\n")
    slots = [f"{color}({LARGEST_COUNT})" for color in COLORS] + ["Empty", "Empty"]
    launchers = ["[Board]", "Width 3", "Height 3", "", "[Layout]"]
    launchers += [" ".join(slots[row:row + 3]) for row in range(0, len(slots), 3)]
    (OUTPUT_DIRECTORY / f"{name}_SpawnBall.txt").write_text("\n".join(launchers) + "\n", newline="\n")
    return len(types)


def main():
    for name, width, height, is_brick, hits, open_rows in LEVELS:
        bricks = write_level(name, width, height, is_brick, hits, open_rows, random.Random(f"{SEED}-{name}"))
        print(f"{name}: {bricks} bricks of {hits} hits")


if __name__ == "__main__":
    main()
