#pragma once

#include "cpu/CpuBoard.h"

template <typename Store>
class CpuBackendBase : public PhysicsBackend {
public:
    void uploadBoard(const BoardUpload& board) override {
        cells.upload(board);
        store.fieldWidth = static_cast<float>(board.columns);
        store.fieldHeight = static_cast<float>(board.rows);
    }
    void addBalls(std::span<const BallSpawn> spawns) override { store.add(spawns); }
    bool acceptsSparseBoardUpdates() const override { return true; }
    void removeBalls(std::span<const uint32_t> descendingIndices) override { store.swapRemove(descendingIndices); }
    size_t ballCount() const override { return store.count; }
    BallSnapshot snapshot() override { return store.snapshot(); }
    std::vector<BallSpawn> exportBalls() override { return store.exportBalls(); }
    std::vector<BallSpawn> takeTail(size_t count) override { return store.takeTail(count); }
    void clear() override { store.clear(); }
    BallTally truncate(size_t newCount) override { return store.truncate(newCount); }

protected:
    BoardView boardView() const { return cells.view(); }

    Store store;
    CpuBoard cells;
};
