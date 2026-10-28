#pragma once

#include "common/BallCount.h"
#include "level/LevelData.h"
#include "sim/BackendFactory.h"

#include <string>

struct EdgeSweepSettings {
    double targetFps = 60.0;
    double frameBudgetShare = 0.8;
    int framesPerWindow = 30;
    int framesBeforeWorstCounts = 10;
    double caseSeconds = 40.0;
    double settleTravels = 1.5;
    double longestSettleSeconds = 20.0;
    double refillBelowBudget = 0.85;
    double refillSettleSeconds = 3.0;
    float screenWidth = 1920.0f;
    float screenHeight = 1080.0f;
    bool drawing = true;
};

struct EdgeSweepResult {
    double ballsAtBudget = 0.0;
    double windowFrameMs = 0.0;
    double filledBalls = 0.0;
    double settledFrameMs = 0.0;
    double worstFrameMs = 0.0;
    double launchNanosecondsPerBall = 0.0;
    BallCount launchedBalls = 0;
    int aliveBricks = 0;
    std::string stopReason;
    double seconds = 0.0;
};

class Simulation;

// Sizes the visibility grid as the game would on a screen of the settings' size.
void useGameVisibilityGrid(Simulation& simulation, const EdgeSweepSettings& settings = {});

// Fills the board the way the game does, through SpawnPacer, until a window of frames averages more than the 60 FPS
// budget, the backend is full, or the time runs out. The last balls launched are still flying from the launch point,
// cheaper than balls working along the bricks, so the board then runs for one and a half crossings of its height (at
// most 20 s) launching only to replace balls that died, and the result is the settled window's live count, scaled
// down if its frames no longer fit the budget.
EdgeSweepResult sweepEdgeCase(const BrickLayout& layout, BackendKind kind, const EdgeSweepSettings& settings = {});
