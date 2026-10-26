#pragma once

#include "common/BallColor.h"

#include <array>
#include <cstdint>
#include <limits>

using BallCount = uint64_t;
using BallTally = std::array<BallCount, ballColorCount>;

inline constexpr BallCount unlimitedBalls = std::numeric_limits<BallCount>::max();

constexpr BallCount saturatingMultiply(BallCount a, BallCount b) {
    return (a != 0 && b > unlimitedBalls / a) ? unlimitedBalls : a * b;
}

constexpr BallCount remainingCapacity(BallCount limit, BallCount used) { return used >= limit ? 0 : limit - used; }
