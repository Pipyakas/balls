#pragma once

#include "common/BallCount.h"
#include "level/LevelData.h"
#include "sim/BackendFactory.h"

#include <string>

struct EdgeSweepSettings {
    double targetFps = 60.0;
    double frameBudgetShare = 0.8;
    int ticksPerFrame = 2;
    int framesPerWindow = 30;
    int framesBeforeWorstCounts = 10;
    double caseSeconds = 40.0;
};

struct EdgeSweepResult {
    double ballsAtBudget = 0.0;
    double windowFrameMs = 0.0;
    double worstFrameMs = 0.0;
    double launchNanosecondsPerBall = 0.0;
    BallCount launchedBalls = 0;
    int aliveBricks = 0;
    std::string stopReason;
    double seconds = 0.0;
};

// Fills the board the way the game does, through SpawnPacer, so balls launched long ago have reached the bricks while
// new ones are still flying, and stops once a window of frames averages more than the 60 FPS budget (the window's
// average live count is the result), when the backend is full, or when the time runs out.
EdgeSweepResult sweepEdgeCase(const BrickLayout& layout, BackendKind kind, const EdgeSweepSettings& settings = {});
