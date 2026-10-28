#include "game/SpawnPacer.h"

#include <algorithm>
#include <cmath>
#include <limits>

void SpawnPacer::reset() {
    spawnSeconds = initialSecondsPerSpawn;
    lastWorkSeconds = 0.0;
    workPerBall = 0.0;
}

void SpawnPacer::reportFrame(double workSeconds, BallCount liveBalls) {
    if (workSeconds <= 0.0) {
        return;
    }
    lastWorkSeconds = workSeconds;
    workPerBall = liveBalls >= ballsBeforeWorkIsMeasurable ? workSeconds / static_cast<double>(liveBalls) : 0.0;
}

void SpawnPacer::recordSpawn(BallCount balls, double seconds) {
    if (balls < minimumPacedBalls || seconds <= 0.0) {
        return;
    }
    const double measured = seconds / static_cast<double>(balls);
    spawnSeconds += (measured - spawnSeconds) * spawnCostSmoothing;
}

BallCount SpawnPacer::allowance() const {
    const auto toBalls = [](double balls) {
        constexpr double largest = static_cast<double>(std::numeric_limits<BallCount>::max() / 2);
        return static_cast<BallCount>(std::min(std::floor(balls), largest));
    };
    BallCount allowed = toBalls(pacedFrameSeconds * spawnShareOfFrame / spawnSeconds);
    if (workPerBall > 0.0) {
        const double extraWork = std::max(lastWorkSeconds * workGrowthPerFrame, minimumWorkGrowthSeconds);
        allowed = std::min(allowed, toBalls(extraWork / workPerBall));
    }
    return std::max(allowed, minimumPacedBalls);
}
