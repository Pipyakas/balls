#include "game/SpawnPacer.h"

#include <algorithm>
#include <cmath>
#include <limits>

void SpawnPacer::reset() {
    spawnSeconds = initialSecondsPerSpawn;
    lastFrameSeconds = 0.0;
    lastLiveBalls = 0;
}

void SpawnPacer::reportFrame(double frameSeconds, BallCount liveBalls) {
    if (frameSeconds <= 0.0) {
        return;
    }
    lastFrameSeconds = frameSeconds;
    lastLiveBalls = liveBalls;
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
    const double frame = lastFrameSeconds > 0.0 ? lastFrameSeconds : pacedFrameSeconds;
    BallCount allowed = toBalls(std::min(frame, pacedFrameSeconds) * spawnShareOfFrame / spawnSeconds);
    if (lastLiveBalls >= ballsBeforeGrowthIsPaced) {
        const double growth = std::exp2(liveBallDoublingsPerSecond * std::min(frame, longestPacedFrameSeconds)) - 1.0;
        allowed = std::min(allowed, toBalls(static_cast<double>(lastLiveBalls) * growth));
    }
    return std::max(allowed, minimumPacedBalls);
}
