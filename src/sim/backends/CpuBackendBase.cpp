#include "sim/backends/CpuBackendBase.h"

void CpuBackendBase::uploadBoard(const BrickBoard& board, const TargetField& field) {
    paddedCells.assign(board.paddedCells().begin(), board.paddedCells().end());
    brickColors.assign(board.brickColors().begin(), board.brickColors().end());
    brickColors.push_back(-1);
    targetCells.assign(field.targetCells().begin(), field.targetCells().end());
}
