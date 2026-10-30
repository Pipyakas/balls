#pragma once

#include "common/BallCount.h"

// How many balls one frame may add and still leave the physics frame within the 60 FPS budget, from what the last
// frames cost: the step (per tick, growing with the ball count), the additions (per ball) and the rest of the frame.
// Spawning faster than this only queues; nothing a frame adds can push the next one past the budget.
class AdditionGovernor {
public:
    explicit AdditionGovernor(double tickSeconds);

    void recordStep(double seconds, int ticks, BallCount liveBalls);
    void recordAdditions(double seconds, BallCount added);
    void recordFrame(double seconds);
    // freeFlight: nothing on the board is stepped, so the step cost is steady; while bricks are left it varies from
    // frame to frame (kills, the CPU and GPU halves rebalancing), and the step keeps more headroom.
    BallCount allowance(BallCount liveBalls, bool freeFlight) const;
    // The time this frame has for additions, and what adding this many is expected to take.
    double additionSeconds(BallCount liveBalls, bool freeFlight) const;
    double predictedSeconds(BallCount added) const { return secondsPerAddedBall * static_cast<double>(added); }

private:
    double stepSecondsPerBall() const;

    int ticksPerFrame;
    double stepSecondsPerTick = 0.0;
    BallCount stepBalls = 0;
    double lastStepSeconds = 0.0;
    double lastAdditionSeconds = 0.0;
    double otherSeconds = 0.0;
    double secondsPerAddedBall;
};
