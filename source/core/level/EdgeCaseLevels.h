#pragma once

#include "level/LevelData.h"

#include <span>
#include <string>

// Boards that stress one thing each, for measuring backends before the scoring map is known. "Tough" layouts give
// bricks more hits than any run lands, so the board never clears and free flight never starts.
enum class EdgeLayout { Empty, Solid, Tough, Checker, Columns, Shelves, Sparse, LoneBrick, Monochrome, Cage };

struct EdgeBoardSize {
    int width = 0;
    int height = 0;
};

inline constexpr int toughBrickHits = 1'000'000'000;

std::span<const EdgeLayout> allEdgeLayouts();
std::span<const EdgeBoardSize> edgeBoardSizes();
const char* edgeLayoutName(EdgeLayout layout);
std::string edgeCaseName(EdgeLayout layout, EdgeBoardSize size);
BrickLayout generateEdgeLayout(EdgeLayout layout, EdgeBoardSize size);
LevelData generateEdgeLevel(EdgeLayout layout, EdgeBoardSize size);
