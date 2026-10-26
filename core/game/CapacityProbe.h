#pragma once

#include "common/BallCount.h"

#include <optional>

enum class ProbePhase { Idle, Settling, Measuring, Confirming, Done };

inline constexpr double probeTargetFps = 60.0;
inline constexpr double probeSettleSeconds = 0.3;
inline constexpr double probeMeasureSeconds = 0.5;
inline constexpr double probeConfirmSettleSeconds = 1.0;
inline constexpr double probeConfirmMeasureSeconds = 1.5;
inline constexpr double probeBackoffFactor = 0.93;
inline constexpr int probeSlowWindowsToStop = 2;
inline constexpr BallCount probeInitialBalls = 100'000;
inline constexpr BallCount probeMinimumAddition = 1'000;
inline constexpr double probeMinimumGrowth = 0.01;
inline constexpr double probeMaximumGrowth = 1.0;
inline constexpr double probeGrowthResponse = 0.5;

const char* probePhaseName(ProbePhase phase);

class CapacityProbe {
public:
    void start();
    void cancel();
    BallCount update(double frameSeconds, BallCount liveBalls);
    std::optional<BallCount> takeTrimRequest();

    bool isActive() const;
    ProbePhase phase() const { return probePhase; }
    double lastFps() const { return measuredFps; }
    BallCount bestPassingCount() const { return bestPassing; }

private:
    BallCount grownAddition(BallCount liveBalls) const;
    bool measureWindow(double frameSeconds, double settleSeconds, double measureSeconds);
    void beginConfirmation(BallCount candidate);
    BallCount updateRamp(BallCount liveBalls);
    void updateConfirmation();

    ProbePhase probePhase = ProbePhase::Idle;
    double windowElapsed = 0.0;
    double intervalTotal = 0.0;
    BallCount intervalFrames = 0;
    bool settled = false;
    bool firstAdditionDue = false;
    double measuredFps = 0.0;
    BallCount bestPassing = 0;
    int consecutiveSlowWindows = 0;
    std::optional<BallCount> trimRequest;
};
