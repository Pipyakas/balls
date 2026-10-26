#pragma once

#include "cpu/CpuBallStore.h"

class CpuBackendBase : public PhysicsBackend {
public:
    void uploadBoard(const BoardUpload& board) override;
    void addBalls(std::span<const BallSpawn> spawns) override { store.add(spawns); }
    void removeBalls(std::span<const uint32_t> descendingIndices) override { store.swapRemove(descendingIndices); }
    size_t ballCount() const override { return store.count; }
    BallSnapshot snapshot() override { return store.snapshot(); }
    std::vector<BallSpawn> exportBalls() override { return store.exportBalls(); }
    void clear() override { store.clear(); }
    BallTally truncate(size_t newCount) override { return store.truncate(newCount); }

protected:
    BoardView boardView() const {
        return {paddedCells.data(), brickColors.data(), targetCells.data(), nearBrickCells.data()};
    }

    CpuBallStore store;
    SimdVector<int32_t> paddedCells;
    SimdVector<int32_t> brickColors;
    SimdVector<int32_t> targetCells;
    SimdVector<int32_t> nearBrickCells;
};
