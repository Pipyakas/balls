#include "cpu/CpuBackendBase.h"

void CpuBackendBase::uploadBoard(const BoardUpload& board) {
    paddedCells.assign(board.paddedCells.begin(), board.paddedCells.end());
    brickColors.assign(board.brickColors.begin(), board.brickColors.end());
    brickColors.push_back(-1);
    targetCells.assign(board.targetCells.begin(), board.targetCells.end());
    nearBrickCells.assign(board.nearBrickCells.begin(), board.nearBrickCells.end());
    store.fieldWidth = static_cast<float>(board.columns);
    store.fieldHeight = static_cast<float>(board.rows);
}
