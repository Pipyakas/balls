#pragma once

#include "common/BallCount.h"

inline constexpr double pacedFrameSeconds = 1.0 / 60.0;
inline constexpr double spawnShareOfFrame = 0.25;
inline constexpr double liveBallDoublingsPerSecond = 1.0;
inline constexpr double longestPacedFrameSeconds = 0.1;
inline constexpr double initialSecondsPerSpawn = 50e-9;
inline constexpr double spawnCostSmoothing = 0.25;
inline constexpr double deathRateSmoothing = 0.1;
inline constexpr BallCount minimumPacedBalls = 4'096;
inline constexpr BallCount ballsBeforeGrowthIsPaced = 100'000;

class SpawnPacer {
public:
    void reset();
    void reportFrame(double frameSeconds, BallCount liveBalls);
    void recordSpawn(BallCount balls, double seconds);
    BallCount allowance() const;

    double secondsPerSpawn() const { return spawnSeconds; }
    double deathsPerSecond() const { return deathRate; }

private:
    double spawnSeconds = initialSecondsPerSpawn;
    double lastFrameSeconds = 0.0;
    BallCount lastLiveBalls = 0;
    BallCount spawnedSinceReport = 0;
    double deathRate = 0.0;
};
