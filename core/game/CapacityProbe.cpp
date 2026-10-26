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
    const double headroom = measuredFps / targetFramesPerSecond - 1.0;
    const double fraction = std::clamp(headroom * probeGrowthResponse, probeMinimumGrowth, probeMaximumGrowth);
    const BallCount scaled = static_cast<BallCount>(static_cast<double>(liveBalls) * fraction);
    return std::min(std::max(scaled, probeMinimumAddition), remainingCapacity(unlimitedBalls, liveBalls));
}

bool CapacityProbe::measureWindow(double frameSeconds, double settleSeconds, double measureSeconds) {
    windowElapsed += frameSeconds;
    if (!settled) {
        if (windowElapsed >= settleSeconds) {
            settled = true;
            windowElapsed = 0.0;
            intervalTotal = 0.0;
            intervalFrames = 0;
        }
        return false;
    }
    intervalTotal += frameSeconds;
    intervalFrames += 1;
    if (windowElapsed < measureSeconds) {
        return false;
    }
    measuredFps = intervalTotal > 0.0 ? static_cast<double>(intervalFrames) / intervalTotal : 0.0;
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
            beginConfirmation(bestPassing);
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

BallCount CapacityProbe::update(double frameSeconds, BallCount liveBalls) {
    if (!isActive() || frameSeconds <= 0.0) {
        return 0;
    }
    if (firstAdditionDue) {
        firstAdditionDue = false;
        windowElapsed = 0.0;
        return probeInitialBalls;
    }
    if (probePhase == ProbePhase::Confirming) {
        if (measureWindow(frameSeconds, probeConfirmSettleSeconds, probeConfirmMeasureSeconds)) {
            updateConfirmation();
        }
        return 0;
    }
    const bool recheckingSlowWindow = consecutiveSlowWindows > 0;
    const bool windowFinished =
        measureWindow(frameSeconds, recheckingSlowWindow ? probeConfirmSettleSeconds : probeSettleSeconds,
                      recheckingSlowWindow ? probeConfirmMeasureSeconds : probeMeasureSeconds);
    probePhase = settled ? ProbePhase::Measuring : ProbePhase::Settling;
    return windowFinished ? updateRamp(liveBalls) : 0;
}
