#pragma once

#include "common/BallCount.h"
#include "level/LevelData.h"
#include "sim/BackendFactory.h"

#include <string>

struct EdgeSweepSettings {
    double targetFps = 60.0;
    double frameBudgetShare = 0.8;
    int ticksPerFrame = 2;
    double warmupSeconds = 0.3;
    double measureSeconds = 0.4;
    int minimumFrames = 8;
    double caseSeconds = 40.0;
    BallCount firstBalls = 16'384;
    double growth = 2.0;
};

struct EdgeSweepResult {
    BallCount passingBalls = 0;
    double passingFrameMs = 0.0;
    double passingWorstFrameMs = 0.0;
    BallCount failingBalls = 0;
    double failingFrameMs = 0.0;
    double estimatedBalls = 0.0;
    double launchNanosecondsPerBall = 0.0;
    int aliveBricks = 0;
    std::string stopReason;
    double seconds = 0.0;
};

// Launches balls the way the game does and doubles them until a frame of physics no longer fits the 60 FPS budget,
// the backend is full, or the time runs out; the estimate interpolates between the last count that fit and the first
// that did not.
EdgeSweepResult sweepEdgeCase(const BrickLayout& layout, BackendKind kind, const EdgeSweepSettings& settings = {});
