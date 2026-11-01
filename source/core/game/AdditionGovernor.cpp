#include "game/AdditionGovernor.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double targetFramesPerSecond = 60.0;

constexpr double freeFlightBudgetShare = 0.85;
constexpr double steppingBudgetShare = 0.5;

constexpr BallCount smallestScaledBalls = 1 << 16;

constexpr double initialSecondsPerAddedBall = 100e-9;

constexpr BallCount smallestTimedAddition = 1 << 12;

constexpr double largestOtherShare = 0.5;

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

double AdditionGovernor::stepSecondsPerBall() const {
    return stepSecondsPerTick * ticksPerFrame / static_cast<double>(std::max(stepBalls, smallestScaledBalls));
}

double AdditionGovernor::additionSeconds(BallCount liveBalls, bool freeFlight) const {
    const double frameBudgetSeconds = (freeFlight ? freeFlightBudgetShare : steppingBudgetShare) / targetFramesPerSecond;
    const double chargedOtherSeconds = std::min(otherSeconds, frameBudgetSeconds * largestOtherShare);
    return std::max(0.0, frameBudgetSeconds - chargedOtherSeconds - stepSecondsPerBall() * static_cast<double>(liveBalls));
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
