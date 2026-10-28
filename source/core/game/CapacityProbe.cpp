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

void CapacityProbe::start(double targetFramesPerSecondValue, double ticksPerSecond) {
    targetFramesPerSecond = targetFramesPerSecondValue > 0.0 ? targetFramesPerSecondValue : defaultProbeTargetFps;
    ticksPerSecondValue = ticksPerSecond > 0.0 ? ticksPerSecond : defaultProbeTicksPerSecond;
    tickModel = false;
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
    liveBeforeAddition = 0;
    lastAddition = 0;
    stalledAdditions = 0;
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
    const double ticksAtTarget = ticksPerSecondValue / targetFramesPerSecond;
    if (tickModel) {
        const double frameBudget = 1.0 / targetFramesPerSecond - secondsPerFrame;
        target = frameBudget > 0.0 ? live * probePredictionSafety * frameBudget / (ticksAtTarget * secondsPerTick) : live;
    } else if (measuredWorkSeconds > 0.0) {
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

// Least squares of frame time against ticks run: frame = secondsPerFrame + ticks * secondsPerTick. Needs frames with
// different tick counts; at or below the target rate every frame runs the same ticks and the plain average is used.
bool CapacityProbe::fitTickModel() {
    const double frames = static_cast<double>(tickedFrames);
    const double spread = frames * tickSquareSum - tickSum * tickSum;
    if (tickedFrames < 2 || spread <= frames * 1e-6) {
        return false;
    }
    secondsPerTick = (frames * tickFrameSum - tickSum * frameSum) / spread;
    secondsPerFrame = (frameSum - secondsPerTick * tickSum) / frames;
    if (secondsPerTick <= 0.0) {
        return false;
    }
    secondsPerFrame = std::max(secondsPerFrame, 0.0);
    return true;
}

bool CapacityProbe::measureWindow(double frameSeconds, double workSeconds, int ticks, double settleSeconds,
                                  double measureSeconds) {
    windowElapsed += frameSeconds;
    if (!settled) {
        if (windowElapsed >= settleSeconds) {
            settled = true;
            windowElapsed = 0.0;
            intervalTotal = 0.0;
            intervalWork = 0.0;
            intervalFrames = 0;
            tickSum = tickSquareSum = frameSum = tickFrameSum = 0.0;
            tickedFrames = 0;
        }
        return false;
    }
    intervalTotal += frameSeconds;
    intervalWork += workSeconds;
    intervalFrames += 1;
    if (ticks >= 0) {
        tickSum += ticks;
        tickSquareSum += static_cast<double>(ticks) * ticks;
        frameSum += frameSeconds;
        tickFrameSum += ticks * frameSeconds;
        tickedFrames += 1;
    }
    if (windowElapsed < measureSeconds) {
        return false;
    }
    measuredFps = intervalTotal > 0.0 ? static_cast<double>(intervalFrames) / intervalTotal : 0.0;
    measuredWorkSeconds = intervalFrames > 0 ? intervalWork / static_cast<double>(intervalFrames) : 0.0;
    tickModel = fitTickModel();
    if (tickModel) {
        measuredFps = 1.0 / (secondsPerFrame + secondsPerTick * ticksPerSecondValue / targetFramesPerSecond);
    }
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

BallCount CapacityProbe::updateRamp(BallCount liveBalls, BallCount room) {
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
    // Balls that die on bricks of their colour can leave the board as fast as they are launched; when most of the
    // last addition is gone again, the board holds no more however long the probe keeps adding.
    const bool stalled = lastAddition > 0 &&
                         static_cast<double>(liveBalls) < static_cast<double>(liveBeforeAddition) +
                                                              static_cast<double>(lastAddition) * probeStallShare;
    stalledAdditions = stalled ? stalledAdditions + 1 : 0;
    const BallCount addition =
        stalledAdditions >= probeStallsToStop ? 0 : std::min(grownAddition(liveBalls), room);
    liveBeforeAddition = liveBalls;
    lastAddition = addition;
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

BallCount CapacityProbe::update(double frameSeconds, double workSeconds, BallCount liveBalls, BallCount room,
                                int ticks) {
    if (!isActive() || frameSeconds <= 0.0) {
        return 0;
    }
    if (firstAdditionDue) {
        firstAdditionDue = false;
        windowElapsed = 0.0;
        return probeInitialBalls;
    }
    if (probePhase == ProbePhase::Confirming) {
        if (measureWindow(frameSeconds, workSeconds, ticks, probeConfirmSettleSeconds, probeConfirmMeasureSeconds)) {
            updateConfirmation();
        }
        return 0;
    }
    const bool recheckingSlowWindow = consecutiveSlowWindows > 0;
    const bool windowFinished =
        measureWindow(frameSeconds, workSeconds, ticks,
                      recheckingSlowWindow ? probeConfirmSettleSeconds : probeSettleSeconds,
                      recheckingSlowWindow ? probeConfirmMeasureSeconds : probeMeasureSeconds);
    probePhase = settled ? ProbePhase::Measuring : ProbePhase::Settling;
    return windowFinished ? updateRamp(liveBalls, room) : 0;
}
