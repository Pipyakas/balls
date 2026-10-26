#pragma once

#include "cpu/SimdVector.h"
#include "sim/PhysicsBackend.h"

struct CpuBoard {
    SimdVector<int32_t> paddedCells;
    SimdVector<int32_t> brickColors;
    SimdVector<int32_t> targetCells;
    SimdVector<int32_t> nearBrickCells;

    void upload(const BoardUpload& board) {
        paddedCells.assign(board.paddedCells.begin(), board.paddedCells.end());
        brickColors.assign(board.brickColors.begin(), board.brickColors.end());
        brickColors.push_back(-1);
        targetCells.assign(board.targetCells.begin(), board.targetCells.end());
        nearBrickCells.assign(board.nearBrickCells.begin(), board.nearBrickCells.end());
    }

    BoardView view() const { return {paddedCells.data(), brickColors.data(), targetCells.data(), nearBrickCells.data()}; }
    int sentinelBrick() const { return static_cast<int>(brickColors.size()) - 1; }
};
