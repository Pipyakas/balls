#include "sim/TargetField.h"

#include <algorithm>
#include <array>
#include <chrono>

namespace {

constexpr int32_t noTarget = -1;
constexpr int32_t paddingCell = -2;
constexpr int asyncCellThreshold = 1 << 16;

// A rebuild that changed more than this share of the cells is sent whole.
constexpr size_t wholeUploadDivisor = 8;

template <typename T>
bool isReady(const std::future<T>& future) {
    return future.valid() && future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

}

void TargetField::rebuild(const BrickBoard& board) {
    discardPendingRebuilds();
    targets = std::make_shared<const std::vector<int32_t>>(
        compute(copyOf(board), board.paddedCellCount() >= asyncCellThreshold));
    changes.clear();
    sparse = false;
}

bool TargetField::requestRebuild(const BrickBoard& board) {
    const size_t expectedSize = static_cast<size_t>(board.paddedCellCount()) * ballColorCount;
    if (board.paddedCellCount() < asyncCellThreshold || targetCells().size() != expectedSize) {
        rebuild(board);
        return true;
    }
    if (pending.valid()) {
        rebuildQueued = true;
    } else {
        launch(copyOf(board));
    }
    return false;
}

bool TargetField::takeFinishedRebuild(const BrickBoard& board) {
    if (!isReady(pending)) {
        return false;
    }
    Rebuilt rebuilt = pending.get();
    targets = std::move(rebuilt.cells);
    changes = std::move(rebuilt.changes);
    sparse = rebuilt.sparse;
    if (rebuildQueued) {
        rebuildQueued = false;
        launch(copyOf(board));
    }
    return true;
}

void TargetField::discardPendingRebuilds() {
    rebuildQueued = false;
    if (pending.valid()) {
        pending.wait();
        pending = {};
    }
}

void TargetField::launch(BoardCopy board) {
    pending = std::async(std::launch::async, [copy = std::move(board), previous = targets] {
        return computeChanges(copy, previous);
    });
}

// Runs on the rebuild thread: a 2048x2048 board's field is 146 MB, and sending all of it after every rebuild held a
// frame for 40 ms, while a rebuild usually changes only the cells around the bricks that broke.
TargetField::Rebuilt TargetField::computeChanges(const BoardCopy& board,
                                                 std::shared_ptr<const std::vector<int32_t>> previous) {
    Rebuilt rebuilt;
    auto cells = std::make_shared<std::vector<int32_t>>(compute(board, true));
    if (previous && previous->size() == cells->size()) {
        const size_t limit = cells->size() / wholeUploadDivisor;
        rebuilt.sparse = true;
        for (size_t cell = 0; cell < cells->size(); ++cell) {
            if ((*cells)[cell] == (*previous)[cell]) {
                continue;
            }
            if (rebuilt.changes.size() / 2 >= limit) {
                rebuilt.sparse = false;
                rebuilt.changes.clear();
                break;
            }
            rebuilt.changes.push_back(static_cast<int32_t>(cell));
            rebuilt.changes.push_back((*cells)[cell]);
        }
    }
    rebuilt.cells = std::move(cells);
    return rebuilt;
}

TargetField::BoardCopy TargetField::copyOf(const BrickBoard& board) {
    return {board.columns(), board.rows(), board.paddedCells(), board.brickColors()};
}

std::vector<int32_t> TargetField::compute(const BoardCopy& board, bool parallelColors) {
    const size_t cellCount = static_cast<size_t>(board.columns + 2) * static_cast<size_t>(board.rows + 2);
    std::vector<int32_t> result(cellCount * ballColorCount);
    if (!parallelColors) {
        for (int colorIndex = 0; colorIndex < ballColorCount; ++colorIndex) {
            floodColor(board, colorIndex, result.data() + static_cast<size_t>(colorIndex) * cellCount);
        }
        return result;
    }
    std::array<std::future<void>, ballColorCount> colors;
    for (int colorIndex = 0; colorIndex < ballColorCount; ++colorIndex) {
        int32_t* colorTargets = result.data() + static_cast<size_t>(colorIndex) * cellCount;
        colors[colorIndex] = std::async(std::launch::async, [&board, colorIndex, colorTargets] {
            floodColor(board, colorIndex, colorTargets);
        });
    }
    for (auto& color : colors) {
        color.wait();
    }
    return result;
}

void TargetField::floodColor(const BoardCopy& board, int colorIndex, int32_t* colorTargets) {
    const int paddedWidth = board.columns + 2;
    const int paddedRows = board.rows + 2;
    const int cellCount = paddedWidth * paddedRows;
    const auto indexOf = [paddedWidth](int column, int row) { return row * paddedWidth + column; };

    std::fill(colorTargets, colorTargets + cellCount, noTarget);
    for (int column = 0; column < paddedWidth; ++column) {
        colorTargets[indexOf(column, 0)] = paddingCell;
        colorTargets[indexOf(column, paddedRows - 1)] = paddingCell;
    }
    for (int row = 0; row < paddedRows; ++row) {
        colorTargets[indexOf(0, row)] = paddingCell;
        colorTargets[indexOf(paddedWidth - 1, row)] = paddingCell;
    }

    std::vector<int32_t> frontier;
    for (int row = 1; row <= board.rows; ++row) {
        for (int column = 1; column <= board.columns; ++column) {
            const int index = indexOf(column, row);
            const int brick = board.cells[index];
            if (brick >= 0 && board.brickColors[brick] == colorIndex) {
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
            if (colorTargets[neighbor] == noTarget) {
                colorTargets[neighbor] = colorTargets[index];
                frontier.push_back(neighbor);
            }
        }
    }

    for (int row = 0; row < paddedRows; ++row) {
        for (int column = 0; column < paddedWidth; ++column) {
            const int index = indexOf(column, row);
            if (colorTargets[index] != paddingCell) {
                continue;
            }
            if (frontier.empty()) {
                colorTargets[index] = noTarget;
                continue;
            }
            const int clampedColumn = column < 1 ? 1 : (column > board.columns ? board.columns : column);
            const int clampedRow = row < 1 ? 1 : (row > board.rows ? board.rows : row);
            colorTargets[index] = colorTargets[indexOf(clampedColumn, clampedRow)];
        }
    }
}
