#pragma once

#include "common/BallCount.h"

class AdditionGovernor {
public:
    explicit AdditionGovernor(double tickSeconds);

    void recordStep(double seconds, int ticks, BallCount liveBalls);
    void recordAdditions(double seconds, BallCount added);
    void recordFrame(double seconds);

    BallCount allowance(BallCount liveBalls, bool freeFlight) const;

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
