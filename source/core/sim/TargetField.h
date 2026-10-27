#pragma once

#include "sim/BrickBoard.h"

#include <cstdint>
#include <future>
#include <vector>

class TargetField {
public:
    void rebuild(const BrickBoard& board);
    bool requestRebuild(const BrickBoard& board);
    bool takeFinishedRebuild(const BrickBoard& board);
    void discardPendingRebuilds();

    const std::vector<int32_t>& targetCells() const { return targets; }

private:
    struct BoardCopy {
        int columns = 0;
        int rows = 0;
        std::vector<int32_t> cells;
        std::vector<int32_t> brickColors;
    };

    static BoardCopy copyOf(const BrickBoard& board);
    static std::vector<int32_t> compute(const BoardCopy& board, bool parallelColors);
    static void floodColor(const BoardCopy& board, int colorIndex, int32_t* colorTargets);
    void launch(BoardCopy board);

    std::vector<int32_t> targets;
    std::future<std::vector<int32_t>> pending;
    bool rebuildQueued = false;
};
