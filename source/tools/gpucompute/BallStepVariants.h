#pragma once

#include <span>
#include <string_view>

// One compiled copy of BallStep.hlsl per profile row in source/core/gpu/BallStepProfiles.def.
struct BallStepBlobSet {
    std::span<const unsigned char> ballStep;
    std::span<const unsigned char> countCells;
    std::span<const unsigned char> scanCells;
    std::span<const unsigned char> stepAndSort;

    bool empty() const { return ballStep.empty(); }
};

// `variant` is a BallStepTuning::variant name. An unknown name returns the first profile's blobs.
BallStepBlobSet ballStepSpirvSet(std::string_view variant);
BallStepBlobSet ballStepDxilSet(std::string_view variant);
