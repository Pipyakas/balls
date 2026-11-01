#include "TestFramework.h"

#include "sim/BackendFactory.h"
#include "sim/BrickBoard.h"

#include <vector>

namespace {

void uploadEmptyBoard(PhysicsBackend& backend, int columns, int rows) {
    BrickLayout layout;
    layout.width = columns;
    layout.height = rows;
    layout.playableHeight = rows;
    layout.cellIds.assign(static_cast<size_t>(columns) * static_cast<size_t>(rows), emptyBrickId);
    BrickBoard board;
    board.load(layout);
    const std::vector<int32_t> hitsLeft = board.hitsLeftPerBrick();
    backend.uploadBoard({board.columns(), board.rows(), board.paddedCells(), board.brickColors(), {},
                         board.nearBrickFlags(), hitsLeft, {}, false});
}

}

TEST_CASE("a preallocated CUDA backend carves its balls around a board too big for the reserve") {
    if (!isBackendAvailable(BackendKind::Cuda)) {
        return;
    }
    BackendHandle cuda = createBackend(BackendKind::Cuda);
    const size_t preallocated = cuda ? cuda->preallocateBalls() : 0;
    if (preallocated == 0) {
        return;
    }
    uploadEmptyBoard(*cuda, 30, 38);
    EXPECT(cuda->ballCapacityLimit() == preallocated);

    uploadEmptyBoard(*cuda, 8192, 8192);
    const size_t underHugeBoard = cuda->ballCapacityLimit();
    EXPECT(underHugeBoard > 0 && underHugeBoard < preallocated);
    EXPECT(cuda->faultMessage().empty());
    uploadEmptyBoard(*cuda, 30, 38);
    EXPECT(cuda->ballCapacityLimit() == preallocated);
    EXPECT(cuda->faultMessage().empty());
}
