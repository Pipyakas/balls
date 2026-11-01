#pragma once

#include "sim/BrickBoard.h"

#include <cstdint>
#include <future>
#include <memory>
#include <span>
#include <vector>

class TargetField {
public:
    void rebuild(const BrickBoard& board);
    bool requestRebuild(const BrickBoard& board);
    bool takeFinishedRebuild(const BrickBoard& board);
    void discardPendingRebuilds();

    const std::vector<int32_t>& targetCells() const { return targets ? *targets : noTargets; }

    bool lastRebuildIsSparse() const { return sparse; }
    std::span<const int32_t> lastRebuildChanges() const { return changes; }

private:
    struct BoardCopy {
        int columns = 0;
        int rows = 0;
        std::vector<int32_t> cells;
        std::vector<int32_t> brickColors;
    };

    static BoardCopy copyOf(const BrickBoard& board);
    struct Rebuilt {
        std::shared_ptr<const std::vector<int32_t>> cells;
        std::vector<int32_t> changes;
        bool sparse = false;
    };

    static std::vector<int32_t> compute(const BoardCopy& board, bool parallelColors);
    static Rebuilt computeChanges(const BoardCopy& board, std::shared_ptr<const std::vector<int32_t>> previous);
    static void floodColor(const BoardCopy& board, int colorIndex, int32_t* colorTargets);
    void launch(BoardCopy board);

    inline static const std::vector<int32_t> noTargets;
    std::shared_ptr<const std::vector<int32_t>> targets;
    std::vector<int32_t> changes;
    bool sparse = false;
    std::future<Rebuilt> pending;
    bool rebuildQueued = false;
};
