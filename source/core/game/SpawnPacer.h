#pragma once

#include "common/BallCount.h"

// How many balls one frame may add without that frame, or the frames after it, standing out.
//
// Two costs bound a frame's additions: making the balls (generating and handing them to the backend, paid in the
// frame that adds them) and stepping and drawing them (paid by every frame after). Making them may take a quarter of
// the frame, and the balls on the board may at most double per second, so frame time rises gradually as the scene
// fills instead of jumping when a launcher, the probe or a fill releases millions at once. Both are measured per
// second of play, not per frame, so a game drawing hundreds of frames a second fills no faster than one at 60.
inline constexpr double pacedFrameSeconds = 1.0 / 60.0;
inline constexpr double spawnShareOfFrame = 0.25;
inline constexpr double liveBallDoublingsPerSecond = 1.0;
inline constexpr double longestPacedFrameSeconds = 0.1;
inline constexpr double initialSecondsPerSpawn = 50e-9;
inline constexpr double spawnCostSmoothing = 0.25;
inline constexpr BallCount minimumPacedBalls = 4'096;
inline constexpr BallCount ballsBeforeGrowthIsPaced = 100'000;

class SpawnPacer {
public:
    void reset();
    void reportFrame(double frameSeconds, BallCount liveBalls);
    void recordSpawn(BallCount balls, double seconds);
    BallCount allowance() const;

    double secondsPerSpawn() const { return spawnSeconds; }

private:
    double spawnSeconds = initialSecondsPerSpawn;
    double lastFrameSeconds = 0.0;
    BallCount lastLiveBalls = 0;
};
