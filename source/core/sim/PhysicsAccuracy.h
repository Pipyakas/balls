#pragma once

#include "level/LevelData.h"
#include "sim/BackendFactory.h"
#include "sim/PhysicsBackend.h"
#include "sim/Simulation.h"

#include <string>
#include <vector>

BackendHandle createReferenceBackend(double longestSubStep = 0.01);

struct AccuracyScenario {
    std::string name;
    BrickLayout level;
    size_t balls = 0;

    bool launch = true;
    int frames = 0;
    int ticksPerFrame = 2;
    int sampleEveryFrames = 60;

    bool visibilityGrid = true;

    bool drawing = true;

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

    bool comparedPositions = false;
    double meanPositionError = 0.0;
    double p99PositionError = 0.0;
    double maxPositionError = 0.0;

    double worstBrickGap = 0.0;
    double worstBallGap = 0.0;
    double worstHitGap = 0.0;
    size_t worstOutsideField = 0;
    size_t worstInsideBricks = 0;
    double worstSpeedError = 0.0;
};

AccuracyReport compareToReference(const AccuracyRun& run, const AccuracyRun& reference);
std::string describeAccuracy(std::string_view backend, const AccuracyReport& report);
