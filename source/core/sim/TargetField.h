#pragma once

#include "sim/BrickBoard.h"

#include <cstdint>
#include <vector>

class TargetField {
public:
    void rebuild(const BrickBoard& board);

    const std::vector<int32_t>& targetCells() const { return targets; }

private:
    void floodColor(const BrickBoard& board, int colorIndex);

    std::vector<int32_t> targets;
    std::vector<int32_t> frontier;
};
