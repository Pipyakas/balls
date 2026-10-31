#pragma once

#include "common/BallColor.h"

#include <array>
#include <cstdint>
#include <limits>
#include <string>

using BallCount = uint64_t;
using BallTally = std::array<BallCount, ballColorCount>;

inline constexpr BallCount unlimitedBalls = std::numeric_limits<BallCount>::max();

constexpr BallCount saturatingMultiply(BallCount a, BallCount b) {
    return (a != 0 && b > unlimitedBalls / a) ? unlimitedBalls : a * b;
}

constexpr BallCount remainingCapacity(BallCount limit, BallCount used) { return used >= limit ? 0 : limit - used; }

// "1,234,567".
inline std::string groupedCount(BallCount value) {
    std::string digits = std::to_string(value);
    for (int insertAt = static_cast<int>(digits.size()) - 3; insertAt > 0; insertAt -= 3) {
        digits.insert(static_cast<size_t>(insertAt), ",");
    }
    return digits;
}
