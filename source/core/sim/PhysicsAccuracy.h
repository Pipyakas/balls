#pragma once

#include "level/LevelData.h"
#include "sim/BackendFactory.h"
#include "sim/PhysicsBackend.h"
#include "sim/Simulation.h"

#include <string>
#include <vector>

// Checks that an optimized backend still plays the same game as ReferenceBackend, so a speed-up can never come
// from quietly changing the physics. Both run the same Simulation (same spawns, board, hit rules and settling);
// only the ball stepping differs.

// A finer reference (shorter sub-steps) differs from the default one only by chaos: brick hits amplify tiny
// differences, so the two give the noise floor an optimized backend's gaps are judged against.
BackendHandle createReferenceBackend(double longestSubStep = 0.01);

struct AccuracyScenario {
    std::string name;
    BrickLayout level;
    size_t balls = 0;
    // Launch from the launch point like released balls, or scatter over the board.
    bool launch = true;
    int frames = 0;
    int ticksPerFrame = 2;
    int sampleEveryFrames = 60;
    // The game's visibility grid; without it no backend lets balls fly freely, so stepping itself is measured.
    bool visibilityGrid = true;
    // Off as with the board drawing turned off in the game: no grid, no render copy, free flight without drawing.
    bool drawing = true;
    // The game's physics rate; the scenario covers the same game time at any rate.
    int ticksPerSecond = defaultTicksPerSecond;
};

struct AccuracySample {
    int frame = 0;
    int bricks = 0;
    size_t balls = 0;
    unsigned long long hits = 0;
    size_t outsideField = 0;
    size_t insideBricks = 0;
    double worstSpeedError = 0.0;
};

struct AccuracyRun {
    std::vector<AccuracySample> samples;
    std::vector<BallSpawn> finalBalls;
    std::string fault;
};

AccuracyRun runAccuracyScenario(BackendHandle backend, const AccuracyScenario& scenario);

struct AccuracyReport {
    // Per-ball distance from the reference at the end; only when no ball was removed, so indices still match.
    bool comparedPositions = false;
    double meanPositionError = 0.0;
    double p99PositionError = 0.0;
    double maxPositionError = 0.0;
    // Worst relative gap over the samples, against max(reference, slack).
    double worstBrickGap = 0.0;
    double worstBallGap = 0.0;
    double worstHitGap = 0.0;
    size_t worstOutsideField = 0;
    size_t worstInsideBricks = 0;
    double worstSpeedError = 0.0;
};

AccuracyReport compareToReference(const AccuracyRun& run, const AccuracyRun& reference);
std::string describeAccuracy(std::string_view backend, const AccuracyReport& report);
