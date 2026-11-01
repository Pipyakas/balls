#pragma once

#include <span>
#include <string_view>

struct BallStepBlobSet {
    std::span<const unsigned char> ballStep;
    std::span<const unsigned char> countCells;
    std::span<const unsigned char> scanCells;
    std::span<const unsigned char> stepAndSort;

    bool empty() const { return ballStep.empty(); }
};

BallStepBlobSet ballStepSpirvSet(std::string_view variant);
BallStepBlobSet ballStepDxilSet(std::string_view variant);
