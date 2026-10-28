#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

class SortSchedule {
public:
    static constexpr std::array<int, 3> intervalCandidates{0, 8, 16};
    static constexpr int warmupTicks = 16;
    static constexpr int measuredTicks = 32;
    static constexpr int ticksBetweenTrials = 1200;
    static constexpr size_t minimumBalls = size_t{1} << 20;
    static constexpr size_t bulkAddDenominator = 32;

    bool sortThisTick(size_t balls) {
        if (balls < minimumBalls || intervalTicks <= 0 || ++ticksSinceSort < intervalTicks) {
            return false;
        }
        ticksSinceSort = 0;
        return true;
    }

    // Balls appended in bulk (a composer moving them in from another device) arrive unsorted and change the set, so
    // a trial measuring meanwhile would charge the arrival to its candidate: that candidate is measured again.
    // Spawns in play are far below the threshold.
    void ballsAdded(size_t added, size_t balls) {
        if (ticksUntilTrial == 0 && added * bulkAddDenominator >= balls) {
            trialTick = 0;
            trialMsPerBall = 0.0;
        }
    }

    void record(double kernelMs, size_t balls) {
        if (balls < minimumBalls) {
            return;
        }
        if (ticksUntilTrial > 0) {
            if (--ticksUntilTrial == 0) {
                candidate = 0;
                trialTick = 0;
                trialMsPerBall = 0.0;
                intervalTicks = intervalCandidates[0];
                ticksSinceSort = intervalTicks;
            }
            return;
        }
        if (trialTick++ >= warmupTicks) {
            trialMsPerBall += kernelMs / static_cast<double>(balls);
        }
        if (trialTick < warmupTicks + measuredTicks) {
            return;
        }
        candidateCost[candidate] = trialMsPerBall / measuredTicks;
        trialTick = 0;
        trialMsPerBall = 0.0;
        if (++candidate < intervalCandidates.size()) {
            intervalTicks = intervalCandidates[candidate];
            ticksSinceSort = intervalTicks;
            return;
        }
        const auto cheapest = std::ranges::min_element(candidateCost);
        intervalTicks = intervalCandidates[static_cast<size_t>(cheapest - candidateCost.begin())];
        ticksSinceSort = 0;
        ticksUntilTrial = ticksBetweenTrials;
    }

    int interval() const { return intervalTicks; }

private:
    int ticksSinceSort = 0;
    int intervalTicks = 0;
    size_t candidate = 0;
    int trialTick = 0;
    double trialMsPerBall = 0.0;
    int ticksUntilTrial = 0;
    std::array<double, intervalCandidates.size()> candidateCost{};
};
