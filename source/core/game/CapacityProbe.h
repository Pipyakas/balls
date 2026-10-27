#pragma once

#include "common/BallCount.h"

#include <limits>
#include <optional>

enum class ProbePhase { Idle, Settling, Measuring, Confirming, Done };

inline constexpr double defaultProbeTargetFps = 60.0;
inline constexpr double probePassTolerance = 0.98;
inline constexpr double probeSettleSeconds = 0.3;
inline constexpr double probeMeasureSeconds = 0.5;
inline constexpr double probeConfirmSettleSeconds = 1.0;
inline constexpr double probeConfirmMeasureSeconds = 1.5;
inline constexpr double probeBackoffFactor = 0.93;
inline constexpr int probeSlowWindowsToStop = 2;
inline constexpr BallCount probeInitialBalls = 100'000;
inline constexpr BallCount probeMinimumAddition = 1'000;
inline constexpr double probeMinimumGrowth = 0.05;
inline constexpr double probeMaximumGrowth = 1.0;
inline constexpr double probeGrowthResponse = 0.5;
inline constexpr double probePredictionSafety = 0.97;
inline constexpr double probeMaximumJump = 4.0;
inline constexpr double probeBisectTolerance = 0.03;

const char* probePhaseName(ProbePhase phase);

class CapacityProbe {
public:
    void start(double targetFramesPerSecond = defaultProbeTargetFps);
    void cancel();
    // room: how many more balls the backend can hold; reaching it ends the ramp like a slow frame would.
    BallCount update(double frameSeconds, double workSeconds, BallCount liveBalls,
                     BallCount room = std::numeric_limits<BallCount>::max());
    std::optional<BallCount> takeTrimRequest();

    bool isActive() const;
    ProbePhase phase() const { return probePhase; }
    double lastFps() const { return measuredFps; }
    double targetFps() const { return targetFramesPerSecond; }
    BallCount bestPassingCount() const { return bestPassing; }

private:
    BallCount grownAddition(BallCount liveBalls) const;
    bool measuredFpsPasses() const;
    bool measureWindow(double frameSeconds, double workSeconds, double settleSeconds, double measureSeconds);
    void handleSlowCount(BallCount liveBalls);
    void beginConfirmation(BallCount candidate);
    BallCount updateRamp(BallCount liveBalls, BallCount room);
    void updateConfirmation();

    ProbePhase probePhase = ProbePhase::Idle;
    double windowElapsed = 0.0;
    double intervalTotal = 0.0;
    double intervalWork = 0.0;
    double measuredWorkSeconds = 0.0;
    BallCount lowestFailing = 0;
    BallCount intervalFrames = 0;
    bool settled = false;
    bool firstAdditionDue = false;
    double measuredFps = 0.0;
    double targetFramesPerSecond = defaultProbeTargetFps;
    BallCount bestPassing = 0;
    int consecutiveSlowWindows = 0;
    std::optional<BallCount> trimRequest;
};
