#include "game/CapacityProbe.h"

#include <algorithm>

const char* probePhaseName(ProbePhase phase) {
    switch (phase) {
    case ProbePhase::Idle:
        return "idle";
    case ProbePhase::Settling:
        return "settling";
    case ProbePhase::Measuring:
        return "measuring";
    case ProbePhase::Confirming:
        return "confirming";
    case ProbePhase::Done:
        return "done";
    }
    return "idle";
}

bool CapacityProbe::measuredFpsPasses() const { return measuredFps >= targetFramesPerSecond * probePassTolerance; }

bool CapacityProbe::isActive() const {
    return probePhase == ProbePhase::Settling || probePhase == ProbePhase::Measuring ||
           probePhase == ProbePhase::Confirming;
}

void CapacityProbe::start(double targetFramesPerSecondValue) {
    targetFramesPerSecond = targetFramesPerSecondValue > 0.0 ? targetFramesPerSecondValue : defaultProbeTargetFps;
    probePhase = ProbePhase::Settling;
    windowElapsed = 0.0;
    intervalTotal = 0.0;
    intervalFrames = 0;
    settled = false;
    firstAdditionDue = true;
    measuredFps = 0.0;
    bestPassing = 0;
    lowestFailing = 0;
    measuredWorkSeconds = 0.0;
    consecutiveSlowWindows = 0;
    trimRequest.reset();
}

void CapacityProbe::cancel() {
    probePhase = ProbePhase::Idle;
    firstAdditionDue = false;
    trimRequest.reset();
}

std::optional<BallCount> CapacityProbe::takeTrimRequest() {
    const std::optional<BallCount> request = trimRequest;
    trimRequest.reset();
    return request;
}

BallCount CapacityProbe::grownAddition(BallCount liveBalls) const {
    const double live = static_cast<double>(std::max<BallCount>(liveBalls, 1));
    double target = 0.0;
    if (measuredWorkSeconds > 0.0) {
        target = live * probePredictionSafety / (measuredWorkSeconds * targetFramesPerSecond);
    } else {
        const double headroom = measuredFps / targetFramesPerSecond - 1.0;
        target = live * (1.0 + std::clamp(headroom * probeGrowthResponse, probeMinimumGrowth, probeMaximumGrowth));
    }
    target = std::clamp(target, live * (1.0 + probeMinimumGrowth), live * probeMaximumJump);
    if (lowestFailing > 0) {
        target = std::min(target, (live + static_cast<double>(lowestFailing)) * 0.5);
    }
    const BallCount addition = target > live ? static_cast<BallCount>(target - live) : 0;
    return std::min(std::max(addition, probeMinimumAddition), remainingCapacity(unlimitedBalls, liveBalls));
}

void CapacityProbe::handleSlowCount(BallCount liveBalls) {
    lowestFailing = lowestFailing == 0 ? liveBalls : std::min(lowestFailing, liveBalls);
    const double gap = static_cast<double>(lowestFailing) - static_cast<double>(bestPassing);
    if (bestPassing > 0 && gap > static_cast<double>(bestPassing) * probeBisectTolerance) {
        trimRequest = bestPassing + static_cast<BallCount>(gap * 0.5);
        consecutiveSlowWindows = 0;
        windowElapsed = 0.0;
        settled = false;
        return;
    }
    beginConfirmation(bestPassing);
}

bool CapacityProbe::measureWindow(double frameSeconds, double workSeconds, double settleSeconds, double measureSeconds) {
    windowElapsed += frameSeconds;
    if (!settled) {
        if (windowElapsed >= settleSeconds) {
            settled = true;
            windowElapsed = 0.0;
            intervalTotal = 0.0;
            intervalWork = 0.0;
            intervalFrames = 0;
        }
        return false;
    }
    intervalTotal += frameSeconds;
    intervalWork += workSeconds;
    intervalFrames += 1;
    if (windowElapsed < measureSeconds) {
        return false;
    }
    measuredFps = intervalTotal > 0.0 ? static_cast<double>(intervalFrames) / intervalTotal : 0.0;
    measuredWorkSeconds = intervalFrames > 0 ? intervalWork / static_cast<double>(intervalFrames) : 0.0;
    windowElapsed = 0.0;
    settled = false;
    return true;
}

void CapacityProbe::beginConfirmation(BallCount candidate) {
    probePhase = ProbePhase::Confirming;
    bestPassing = candidate;
    trimRequest = candidate;
    windowElapsed = 0.0;
    settled = false;
}

BallCount CapacityProbe::updateRamp(BallCount liveBalls) {
    if (!measuredFpsPasses()) {
        consecutiveSlowWindows += 1;
        if (consecutiveSlowWindows >= probeSlowWindowsToStop) {
            handleSlowCount(liveBalls);
        }
        return 0;
    }
    if (consecutiveSlowWindows > 0) {
        consecutiveSlowWindows = 0;
        return 0;
    }
    consecutiveSlowWindows = 0;
    bestPassing = liveBalls;
    const BallCount addition = grownAddition(liveBalls);
    if (addition == 0) {
        beginConfirmation(bestPassing);
    }
    return addition;
}

void CapacityProbe::updateConfirmation() {
    if (measuredFpsPasses()) {
        probePhase = ProbePhase::Done;
        return;
    }
    beginConfirmation(static_cast<BallCount>(static_cast<double>(bestPassing) * probeBackoffFactor));
}

BallCount CapacityProbe::update(double frameSeconds, double workSeconds, BallCount liveBalls) {
    if (!isActive() || frameSeconds <= 0.0) {
        return 0;
    }
    if (firstAdditionDue) {
        firstAdditionDue = false;
        windowElapsed = 0.0;
        return probeInitialBalls;
    }
    if (probePhase == ProbePhase::Confirming) {
        if (measureWindow(frameSeconds, workSeconds, probeConfirmSettleSeconds, probeConfirmMeasureSeconds)) {
            updateConfirmation();
        }
        return 0;
    }
    const bool recheckingSlowWindow = consecutiveSlowWindows > 0;
    const bool windowFinished =
        measureWindow(frameSeconds, workSeconds, recheckingSlowWindow ? probeConfirmSettleSeconds : probeSettleSeconds,
                      recheckingSlowWindow ? probeConfirmMeasureSeconds : probeMeasureSeconds);
    probePhase = settled ? ProbePhase::Measuring : ProbePhase::Settling;
    return windowFinished ? updateRamp(liveBalls) : 0;
}
