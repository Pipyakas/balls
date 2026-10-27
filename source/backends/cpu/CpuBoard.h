#pragma once

#include "cpu/SimdVector.h"
#include "sim/PhysicsBackend.h"

#include <vector>

struct CpuBoard {
    SimdVector<int32_t> paddedCells;
    SimdVector<int32_t> brickColors;
    SimdVector<int32_t> targetCells;
    SimdVector<int32_t> nearBrickCells;
    std::vector<int32_t> nearCellsPerRow;
    int paddedWidth = 1;
    int nearRowsEnd = 0;

    void upload(const BoardUpload& board) {
        if (board.onlyChangedCells && paddedCells.size() == board.paddedCells.size()) {
            for (const int32_t index : board.changedCells) {
                const size_t cell = static_cast<size_t>(index);
                paddedCells[cell] = board.paddedCells[cell];
                nearCellsPerRow[cell / static_cast<size_t>(paddedWidth)] +=
                    (board.nearBrickCells[cell] != 0) - (nearBrickCells[cell] != 0);
                nearBrickCells[cell] = board.nearBrickCells[cell];
            }
            if (!board.targetCells.empty()) {
                targetCells.assign(board.targetCells.begin(), board.targetCells.end());
            }
            findNearRowsEnd();
            return;
        }
        paddedWidth = board.columns + 2;
        nearCellsPerRow.assign(static_cast<size_t>(board.rows) + 2, 0);
        for (size_t cell = 0; cell < board.nearBrickCells.size(); ++cell) {
            nearCellsPerRow[cell / static_cast<size_t>(paddedWidth)] += board.nearBrickCells[cell] != 0;
        }
        findNearRowsEnd();
        paddedCells.assign(board.paddedCells.begin(), board.paddedCells.end());
        brickColors.assign(board.brickColors.begin(), board.brickColors.end());
        brickColors.push_back(-1);
        if (!board.targetCells.empty()) {
            targetCells.assign(board.targetCells.begin(), board.targetCells.end());
        }
        nearBrickCells.assign(board.nearBrickCells.begin(), board.nearBrickCells.end());
    }

    void findNearRowsEnd() {
        int paddedRow = static_cast<int>(nearCellsPerRow.size());
        while (paddedRow > 0 && nearCellsPerRow[static_cast<size_t>(paddedRow) - 1] == 0) {
            --paddedRow;
        }
        nearRowsEnd = paddedRow;
    }

    BoardView view() const { return {paddedCells.data(), brickColors.data(), targetCells.data(), nearBrickCells.data()}; }
    int sentinelBrick() const { return static_cast<int>(brickColors.size()) - 1; }
};
