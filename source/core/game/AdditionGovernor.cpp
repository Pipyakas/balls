#include "game/AdditionGovernor.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double targetFramesPerSecond = 60.0;
// The share of 1/60 s the physics frame plans to use; the rest absorbs a frame that costs more than the last one.
constexpr double freeFlightBudgetShare = 0.85;
constexpr double steppingBudgetShare = 0.5;
// Below this many balls a step is mostly fixed cost, so its per-ball cost is taken over at least this many.
constexpr BallCount smallestScaledBalls = 1 << 16;
// Before any addition has been timed.
constexpr double initialSecondsPerAddedBall = 100e-9;
// Too few balls to time an addition by.
constexpr BallCount smallestTimedAddition = 1 << 12;

}

AdditionGovernor::AdditionGovernor(double tickSeconds)
    : ticksPerFrame(std::max(1, static_cast<int>(std::ceil(1.0 / (targetFramesPerSecond * tickSeconds) - 1e-9)))),
      secondsPerAddedBall(initialSecondsPerAddedBall) {}

void AdditionGovernor::recordStep(double seconds, int ticks, BallCount liveBalls) {
    lastStepSeconds = seconds;
    if (ticks > 0) {
        stepSecondsPerTick = seconds / ticks;
        stepBalls = liveBalls;
    }
}

void AdditionGovernor::recordAdditions(double seconds, BallCount added) {
    lastAdditionSeconds = seconds;
    if (added >= smallestTimedAddition) {
        secondsPerAddedBall = seconds / static_cast<double>(added);
    }
}

void AdditionGovernor::recordFrame(double seconds) {
    otherSeconds = std::max(0.0, seconds - lastStepSeconds - lastAdditionSeconds);
}

// The next frame steps ticksPerFrame ticks over the live balls plus the added ones, at the last step's cost per
// ball (taken as linear, which overstates free flight, whose cost hardly grows), and adds them at the last
// addition's cost per ball. At most the live count is added, so the linear guess is never stretched past twice.
double AdditionGovernor::stepSecondsPerBall() const {
    return stepSecondsPerTick * ticksPerFrame / static_cast<double>(std::max(stepBalls, smallestScaledBalls));
}

double AdditionGovernor::additionSeconds(BallCount liveBalls, bool freeFlight) const {
    const double frameBudgetSeconds = (freeFlight ? freeFlightBudgetShare : steppingBudgetShare) / targetFramesPerSecond;
    return std::max(0.0, frameBudgetSeconds - otherSeconds - stepSecondsPerBall() * static_cast<double>(liveBalls));
}

BallCount AdditionGovernor::allowance(BallCount liveBalls, bool freeFlight) const {
    const double spare = additionSeconds(liveBalls, freeFlight);
    if (spare <= 0.0) {
        return 0;
    }
    const double balls = spare / (stepSecondsPerBall() + secondsPerAddedBall);
    const BallCount most = std::max(liveBalls, smallestScaledBalls);
    return balls >= static_cast<double>(most) ? most : static_cast<BallCount>(balls);
}
