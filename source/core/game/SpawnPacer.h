#pragma once

#include "common/BallCount.h"

// How many balls one frame may add without that frame, or the frames after it, standing out.
//
// Two costs bound a frame's additions: making the balls (generating and handing them to the backend, paid in the
// frame that adds them) and stepping and drawing them (paid by every frame after). Making them may take a quarter of
// a 60 FPS frame, and the balls on the board may at most double per second on top of replacing the ones that died,
// so frame time rises gradually as the scene fills instead of jumping when a launcher, the probe or a fill releases
// millions at once, while boards whose bricks absorb balls as fast as they arrive still fill.
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
