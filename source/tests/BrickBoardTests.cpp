#include "TestFramework.h"

#include "sim/BrickBoard.h"

#include <algorithm>
#include <random>
#include <vector>

namespace {

constexpr int32_t emptyCell = -1;
constexpr int boardColumns = 24;
constexpr int boardRows = 20;
constexpr int playableRows = 26;
constexpr int boardSeeds = 12;

BrickLayout randomLayout(std::mt19937& random) {
    BrickLayout layout;
    layout.width = boardColumns;
    layout.height = boardRows;
    layout.playableHeight = playableRows;
    layout.cellIds.assign(static_cast<size_t>(boardColumns) * boardRows, emptyBrickId);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::uniform_int_distribution<int> hits(1, 3);
    std::uniform_int_distribution<int> span(2, 3);
    int nextId = 0;
    for (int row = 0; row < boardRows; ++row) {
        for (int column = 0; column < boardColumns; ++column) {
            if (layout.cellIds[static_cast<size_t>(row) * boardColumns + column] != emptyBrickId || unit(random) < 0.2f) {
                continue;
            }
            const int id = nextId++;
            layout.types.push_back({id, static_cast<BallColor>(id % ballColorCount), hits(random)});
            const float shape = unit(random);
            const int width = shape < 0.15f ? std::min(span(random), boardColumns - column) : 1;
            const int height = shape > 0.9f ? std::min(span(random), boardRows - row) : 1;
            for (int dy = 0; dy < height; ++dy) {
                for (int dx = 0; dx < width; ++dx) {
                    int& cell = layout.cellIds[static_cast<size_t>(row + dy) * boardColumns + column + dx];
                    if (cell == emptyBrickId) {
                        cell = id;
                    }
                }
            }
        }
    }
    return layout;
}

struct ReferenceBoard {
    int columns = 0;
    int rows = 0;
    std::vector<int32_t> cells;

    int32_t& at(int column, int row) { return cells[static_cast<size_t>(row + 1) * (columns + 2) + column + 1]; }

    std::vector<std::pair<int, int>> cellsOf(int brick) {
        std::vector<std::pair<int, int>> found;
        for (int row = 0; row < rows; ++row) {
            for (int column = 0; column < columns; ++column) {
                if (at(column, row) == brick) {
                    found.emplace_back(column, row);
                }
            }
        }
        return found;
    }

    void remove(int brick) {
        for (auto [column, row] : cellsOf(brick)) {
            at(column, row) = emptyCell;
        }
    }

    bool raiseOnce(int brick) {
        const auto occupied = cellsOf(brick);
        if (occupied.empty()) {
            return false;
        }
        for (auto [column, row] : occupied) {
            if (row == 0 || (at(column, row - 1) != emptyCell && at(column, row - 1) != brick)) {
                return false;
            }
        }
        for (auto [column, row] : occupied) {
            at(column, row) = emptyCell;
        }
        for (auto [column, row] : occupied) {
            at(column, row - 1) = brick;
        }
        return true;
    }

    void settle(int brickCount) {
        bool moved = true;
        while (moved) {
            moved = false;
            for (int brick = 0; brick < brickCount; ++brick) {
                while (raiseOnce(brick)) {
                    moved = true;
                }
            }
        }
    }
};

std::vector<int32_t> referenceNearFlags(const BrickBoard& board) {
    const int width = board.paddedWidth();
    const int height = board.rows() + 2;
    std::vector<int32_t> flags(board.paddedCells().size(), 0);
    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
            if (board.paddedCells()[static_cast<size_t>(row) * width + column] == emptyCell) {
                continue;
            }
            for (int nearRow = std::max(row - 1, 0); nearRow <= std::min(row + 1, height - 1); ++nearRow) {
                for (int nearColumn = std::max(column - 1, 0); nearColumn <= std::min(column + 1, width - 1); ++nearColumn) {
                    flags[static_cast<size_t>(nearRow) * width + nearColumn] = 1;
                }
            }
        }
    }
    return flags;
}

void killBrick(BrickBoard& board, int brick) {
    while (board.bricks()[brick].isAlive()) {
        board.applyHit(brick);
    }
}

}

TEST_CASE("incremental column settling matches a brute-force rise to a fixed point") {
    for (int seed = 0; seed < boardSeeds; ++seed) {
        std::mt19937 random(static_cast<unsigned>(seed));
        BrickBoard board;
        board.load(randomLayout(random));
        ReferenceBoard reference{board.columns(), board.rows(), board.paddedCells()};
        const int brickCount = static_cast<int>(board.bricks().size());
        std::vector<int> killOrder(static_cast<size_t>(brickCount));
        for (int brick = 0; brick < brickCount; ++brick) {
            killOrder[static_cast<size_t>(brick)] = brick;
        }
        std::ranges::shuffle(killOrder, random);
        killOrder.resize(killOrder.size() / 2);
        for (const int brick : killOrder) {
            killBrick(board, brick);
            board.settleColumns();
            reference.remove(brick);
            reference.settle(brickCount);
            EXPECT(board.paddedCells() == reference.cells);
        }
    }
}

TEST_CASE("near-brick flags and changed cells stay exact while bricks break and rise") {
    std::mt19937 random(7u);
    BrickBoard board;
    board.load(randomLayout(random));
    std::vector<int32_t> mirroredCells = board.paddedCells();
    std::vector<int32_t> mirroredFlags = board.nearBrickFlags();
    board.clearChangedCells();
    const int brickCount = static_cast<int>(board.bricks().size());
    std::uniform_int_distribution<int> pick(0, brickCount - 1);
    for (int round = 0; round < brickCount; ++round) {
        killBrick(board, pick(random));
        board.settleColumns();
        for (const int32_t cell : board.changedCells()) {
            mirroredCells[static_cast<size_t>(cell)] = board.paddedCells()[static_cast<size_t>(cell)];
            mirroredFlags[static_cast<size_t>(cell)] = board.nearBrickFlags()[static_cast<size_t>(cell)];
        }
        board.clearChangedCells();
        EXPECT(mirroredCells == board.paddedCells());
        EXPECT(mirroredFlags == board.nearBrickFlags());
        EXPECT(board.nearBrickFlags() == referenceNearFlags(board));
    }
}

TEST_CASE("a broken brick keeps animating for its break burst, then stops") {
    BrickLayout layout;
    layout.width = 1;
    layout.height = 1;
    layout.playableHeight = 2;
    layout.cellIds = {0};
    layout.types = {{0, BallColor::Red, 1}};
    BrickBoard board;
    board.load(layout);
    EXPECT(board.applyHit(0));
    EXPECT(!board.bricks()[0].isAlive());
    EXPECT(board.bricks()[0].breakBurst == brickBreakBurstSeconds);
    EXPECT(std::ranges::find(board.animatingBrickIndices(), 0) != board.animatingBrickIndices().end());
    board.animate(brickBreakBurstSeconds * 0.5f);
    EXPECT(board.isAnimating());
    board.animate(brickBreakBurstSeconds);
    EXPECT(!board.isAnimating());
}

namespace {

uint8_t expectedShade(const BrickBoard& board, int column, int row) {
    const int brick = board.brickAt(column, row);
    if (brick == emptyCell) {
        return BrickCellImage::emptyShade;
    }
    return BrickCellImage::brickShade(board.brickColors()[static_cast<size_t>(brick)],
                                      board.bricks()[static_cast<size_t>(brick)].hitFlash > 0.0f);
}

}

TEST_CASE("the cell image follows hits, flashes and rises, and its tile revisions name every change") {
    std::mt19937 random(11u);
    BrickBoard board;
    board.load(randomLayout(random));
    const BrickCellImage& image = board.cellImage();
    std::vector<uint8_t> mirror = image.tileBytes();
    std::vector<uint64_t> seen = image.tileRevisions();
    const int brickCount = static_cast<int>(board.bricks().size());
    std::uniform_int_distribution<int> pick(0, brickCount - 1);
    for (int round = 0; round < brickCount * 3; ++round) {
        board.applyHit(pick(random));
        board.settleColumns();
        board.animate(brickHitFlashSeconds * 0.4f);
        for (size_t tile = 0; tile < seen.size(); ++tile) {
            if (image.tileRevisions()[tile] != seen[tile]) {
                seen[tile] = image.tileRevisions()[tile];
                const auto start = image.tileBytes().begin() + static_cast<std::ptrdiff_t>(tile * BrickCellImage::cellsPerTile);
                std::copy(start, start + static_cast<std::ptrdiff_t>(BrickCellImage::cellsPerTile),
                          mirror.begin() + static_cast<std::ptrdiff_t>(tile * BrickCellImage::cellsPerTile));
            }
        }
        EXPECT(mirror == image.tileBytes());
        for (int row = 0; row < board.rows(); ++row) {
            for (int column = 0; column < board.columns(); ++column) {
                EXPECT(image.shadeAt(column, row) == expectedShade(board, column, row));
            }
        }
    }
}

TEST_CASE("a cell image spanning several tiles moves only the revision of the tile it changes") {
    constexpr int columns = BrickCellImage::tileSize * 2 + 3;
    constexpr int rows = BrickCellImage::tileSize + 5;
    BrickCellImage image;
    image.reset(columns, rows);
    EXPECT(image.tilesAcross() == 3);
    EXPECT(image.tilesDown() == 2);
    std::vector<uint8_t> seenIndex(image.tileBytes().size(), 0);
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            EXPECT(++seenIndex[image.indexOf(column, row)] == 1);
        }
    }
    const std::vector<uint64_t> before = image.tileRevisions();
    image.set(columns - 1, rows - 1, BrickCellImage::brickShade(2, true));
    const size_t changedTile = static_cast<size_t>(image.tilesAcross()) * image.tilesDown() - 1;
    for (size_t tile = 0; tile < before.size(); ++tile) {
        EXPECT((image.tileRevisions()[tile] != before[tile]) == (tile == changedTile));
    }
    EXPECT(image.shadeAt(columns - 1, rows - 1) == (3 | BrickCellImage::flashBit));
}
