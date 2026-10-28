#pragma once

#include "common/BallCount.h"

// How many balls one frame may add without that frame, or the frames after it, standing out.
//
// Two costs bound a frame's additions: making the balls (generating and handing them to the backend, paid in the
// frame that adds them) and stepping and drawing them (paid by every frame after). The first is capped at a share of
// the frame; the second may grow by a small fraction per frame, so frame time rises gradually as the scene fills
// instead of jumping when a launcher, the probe or a fill releases millions at once.
inline constexpr double pacedFrameSeconds = 1.0 / 60.0;
inline constexpr double spawnShareOfFrame = 0.25;
inline constexpr double workGrowthPerFrame = 0.04;
inline constexpr double minimumWorkGrowthSeconds = 0.0002;
inline constexpr double initialSecondsPerSpawn = 50e-9;
inline constexpr double spawnCostSmoothing = 0.25;
inline constexpr BallCount minimumPacedBalls = 4'096;
inline constexpr BallCount ballsBeforeWorkIsMeasurable = 10'000;

class SpawnPacer {
public:
    void reset();
    void reportFrame(double workSeconds, BallCount liveBalls);
    void recordSpawn(BallCount balls, double seconds);
    BallCount allowance() const;

    double secondsPerSpawn() const { return spawnSeconds; }
    double workSecondsPerBall() const { return workPerBall; }

private:
    double spawnSeconds = initialSecondsPerSpawn;
    double lastWorkSeconds = 0.0;
    double workPerBall = 0.0;
};
