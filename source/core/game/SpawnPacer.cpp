#include "game/SpawnPacer.h"

#include <algorithm>
#include <cmath>
#include <limits>

void SpawnPacer::reset() {
    spawnSeconds = initialSecondsPerSpawn;
    lastFrameSeconds = 0.0;
    lastLiveBalls = 0;
    spawnedSinceReport = 0;
    deathRate = 0.0;
}

void SpawnPacer::reportFrame(double frameSeconds, BallCount liveBalls) {
    if (frameSeconds <= 0.0) {
        return;
    }
    const BallCount expected = lastLiveBalls + spawnedSinceReport;
    const double died = expected > liveBalls ? static_cast<double>(expected - liveBalls) : 0.0;
    deathRate += (died / frameSeconds - deathRate) * deathRateSmoothing;
    lastFrameSeconds = frameSeconds;
    lastLiveBalls = liveBalls;
    spawnedSinceReport = 0;
}

void SpawnPacer::recordSpawn(BallCount balls, double seconds) {
    spawnedSinceReport += balls;
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
    if (lastLiveBalls >= ballsBeforeGrowthIsPaced) {
        const double frame = std::min(lastFrameSeconds > 0.0 ? lastFrameSeconds : pacedFrameSeconds,
                                      longestPacedFrameSeconds);
        const double growth = std::exp2(liveBallDoublingsPerSecond * frame) - 1.0;
        allowed = std::min(allowed, toBalls(static_cast<double>(lastLiveBalls) * growth + deathRate * frame));
    }
    return std::max(allowed, minimumPacedBalls);
}
