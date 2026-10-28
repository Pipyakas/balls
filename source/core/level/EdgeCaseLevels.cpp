#include "level/EdgeCaseLevels.h"

#include <array>
#include <format>
#include <random>

namespace {

constexpr std::array edgeLayouts{EdgeLayout::Empty,    EdgeLayout::Solid,     EdgeLayout::Tough,
                                 EdgeLayout::Checker,  EdgeLayout::Columns,   EdgeLayout::Shelves,
                                 EdgeLayout::Sparse,   EdgeLayout::LoneBrick, EdgeLayout::Monochrome,
                                 EdgeLayout::Cage};

constexpr std::array edgeSizes{EdgeBoardSize{16, 16},    EdgeBoardSize{48, 48},     EdgeBoardSize{128, 128},
                               EdgeBoardSize{512, 512},  EdgeBoardSize{2048, 2048}, EdgeBoardSize{2048, 64},
                               EdgeBoardSize{64, 2048}};

constexpr int minimumLaunchGapRows = 4;
constexpr int launchGapDivisor = 4;
constexpr int columnWallSpacing = 4;
constexpr int columnDoorSpacing = 8;
constexpr double sparseBrickShare = 0.05;
constexpr int cageHalfWidth = 3;
constexpr int cageHeight = 5;
constexpr int launcherColumns = 7;
constexpr int launcherRows = 4;
constexpr int ballsPerLauncher = 10'000;
constexpr unsigned layoutSeed = 20261028u;

struct LayoutBuilder {
    BrickLayout layout;
    std::mt19937 random{layoutSeed};

    void place(int column, int row, BallColor color, int hits) {
        const int id = static_cast<int>(layout.types.size());
        layout.types.push_back({id, color, hits});
        layout.cellIds[static_cast<size_t>(row) * layout.width + column] = id;
    }
    BallColor randomColor() { return ballColorFromIndex(static_cast<int>(random() % ballColorCount)); }
};

bool inBrickRows(EdgeLayout layout, int column, int row, EdgeBoardSize size, std::mt19937& random) {
    switch (layout) {
    case EdgeLayout::Solid:
    case EdgeLayout::Tough:
    case EdgeLayout::Monochrome:
        return true;
    case EdgeLayout::Checker:
        return column % 2 == 0 && row % 2 == 0;
    case EdgeLayout::Columns:
        return column % columnWallSpacing == 0 && row % columnDoorSpacing != columnDoorSpacing - 1;
    case EdgeLayout::Shelves:
        return row % columnWallSpacing == 0 && column % columnDoorSpacing != columnDoorSpacing - 1;
    case EdgeLayout::Sparse:
        return std::uniform_real_distribution<double>(0.0, 1.0)(random) < sparseBrickShare;
    case EdgeLayout::LoneBrick:
        return column == size.width / 2 && row == 0;
    case EdgeLayout::Empty:
    case EdgeLayout::Cage:
        return false;
    }
    return false;
}

int brickHits(EdgeLayout layout) {
    return layout == EdgeLayout::Solid || layout == EdgeLayout::Monochrome ? 1 : toughBrickHits;
}

}

std::span<const EdgeLayout> allEdgeLayouts() { return edgeLayouts; }

std::span<const EdgeBoardSize> edgeBoardSizes() { return edgeSizes; }

const char* edgeLayoutName(EdgeLayout layout) {
    switch (layout) {
    case EdgeLayout::Empty:
        return "Empty";
    case EdgeLayout::Solid:
        return "Solid";
    case EdgeLayout::Tough:
        return "Tough";
    case EdgeLayout::Checker:
        return "Checker";
    case EdgeLayout::Columns:
        return "Columns";
    case EdgeLayout::Shelves:
        return "Shelves";
    case EdgeLayout::Sparse:
        return "Sparse";
    case EdgeLayout::LoneBrick:
        return "LoneBrick";
    case EdgeLayout::Monochrome:
        return "Monochrome";
    case EdgeLayout::Cage:
        return "Cage";
    }
    return "Unknown";
}

std::string edgeCaseName(EdgeLayout layout, EdgeBoardSize size) {
    return std::format("Edge_{}_{}x{}", edgeLayoutName(layout), size.width, size.height);
}

// Bricks fill the top size.height rows; below them is the launch gap, where the cage closes around the launch point.
BrickLayout generateEdgeLayout(EdgeLayout layout, EdgeBoardSize size) {
    LayoutBuilder builder;
    BrickLayout& bricks = builder.layout;
    const int gapRows = std::max(minimumLaunchGapRows, size.height / launchGapDivisor);
    bricks.width = size.width;
    bricks.height = size.height + gapRows;
    bricks.playableHeight = bricks.height;
    bricks.cellIds.assign(static_cast<size_t>(bricks.width) * bricks.height, emptyBrickId);
    const int hits = brickHits(layout);
    for (int row = 0; row < size.height; ++row) {
        for (int column = 0; column < size.width; ++column) {
            if (inBrickRows(layout, column, row, size, builder.random)) {
                const BallColor color = layout == EdgeLayout::Monochrome ? BallColor::Red : builder.randomColor();
                builder.place(column, row, color, hits);
            }
        }
    }
    if (layout == EdgeLayout::Cage) {
        const int middle = size.width / 2;
        const int bottom = bricks.height - 1;
        for (int row = bottom - cageHeight; row <= bottom; ++row) {
            for (int column = middle - cageHalfWidth; column <= middle + cageHalfWidth; ++column) {
                const bool wall = row == bottom - cageHeight || column == middle - cageHalfWidth ||
                                  column == middle + cageHalfWidth;
                if (wall && column >= 0 && column < size.width) {
                    builder.place(column, row, builder.randomColor(), toughBrickHits);
                }
            }
        }
    }
    return bricks;
}

LevelData generateEdgeLevel(EdgeLayout layout, EdgeBoardSize size) {
    LevelData level;
    level.name = edgeCaseName(layout, size);
    level.bricks = generateEdgeLayout(layout, size);
    level.launchers.columns = launcherColumns;
    level.launchers.visibleRows = launcherRows;
    for (int slot = 0; slot < launcherColumns * launcherRows; ++slot) {
        level.launchers.slots.push_back(Launcher{ballColorFromIndex(slot % ballColorCount), ballsPerLauncher});
    }
    return level;
}
