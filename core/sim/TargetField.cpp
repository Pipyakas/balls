#include "sim/TargetField.h"

#include <array>

namespace {

constexpr int32_t noTarget = -1;

}

void TargetField::rebuild(const BrickBoard& board) {
    targets.assign(static_cast<size_t>(board.paddedCellCount()) * ballColorCount, noTarget);
    for (int colorIndex = 0; colorIndex < ballColorCount; ++colorIndex) {
        floodColor(board, colorIndex);
    }
}

void TargetField::floodColor(const BrickBoard& board, int colorIndex) {
    const int paddedWidth = board.paddedWidth();
    const int cellCount = board.paddedCellCount();
    int32_t* colorTargets = targets.data() + static_cast<size_t>(colorIndex) * cellCount;
    const auto& cells = board.paddedCells();
    const auto& brickColors = board.brickColors();

    frontier.clear();
    for (int row = 0; row < board.rows(); ++row) {
        for (int column = 0; column < board.columns(); ++column) {
            const int index = board.paddedIndex(column, row);
            const int brick = cells[index];
            if (brick >= 0 && brickColors[brick] == colorIndex) {
                colorTargets[index] = index;
                frontier.push_back(index);
            }
        }
    }

    const std::array<int, 8> neighborOffsets{-1, 1, -paddedWidth, paddedWidth,
                                             -paddedWidth - 1, -paddedWidth + 1, paddedWidth - 1, paddedWidth + 1};
    for (size_t head = 0; head < frontier.size(); ++head) {
        const int index = frontier[head];
        for (int offset : neighborOffsets) {
            const int neighbor = index + offset;
            const int column = neighbor % paddedWidth;
            const int row = neighbor / paddedWidth;
            const bool insideBoard = column >= 1 && column <= board.columns() && row >= 1 && row <= board.rows();
            if (!insideBoard || colorTargets[neighbor] != noTarget) {
                continue;
            }
            colorTargets[neighbor] = colorTargets[index];
            frontier.push_back(neighbor);
        }
    }
    for (int index = 0; index < cellCount; ++index) {
        const int column = index % paddedWidth;
        const int row = index / paddedWidth;
        if (colorTargets[index] != noTarget || frontier.empty()) {
            continue;
        }
        const int clampedColumn = column < 1 ? 1 : (column > board.columns() ? board.columns() : column);
        const int clampedRow = row < 1 ? 1 : (row > board.rows() ? board.rows() : row);
        colorTargets[index] = colorTargets[clampedRow * paddedWidth + clampedColumn];
    }
}
