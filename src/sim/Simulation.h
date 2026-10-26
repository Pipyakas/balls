#pragma once

#include "sim/BrickBoard.h"
#include "sim/PhysicsBackend.h"
#include "sim/TargetField.h"

#include <memory>
#include <random>
#include <vector>

struct SimulationTuning {
    float ballRadius = 0.2f;
    float ballSpeed = 14.0f;
    float tickSeconds = 1.0f / 120.0f;
    int maxTicksPerFrame = 4;
    int retargetBounceThreshold = 3;
    float launchHalfAngleDegrees = 55.0f;
};

struct SimulationStats {
    ColorTally ballsByColor{};
    size_t ballCount = 0;
    double lastFramePhysicsMs = 0.0;
    int ticksLastFrame = 0;
    uint64_t totalBallsSpawned = 0;
    uint64_t totalHits = 0;
};

class Simulation {
public:
    explicit Simulation(std::unique_ptr<PhysicsBackend> backend, SimulationTuning tuning = {});

    void load(const BrickLayout& layout);
    void replaceBackend(std::unique_ptr<PhysicsBackend> backend);
    void launchBalls(BallColor color, int count);
    void advance(float frameSeconds);
    void tick();
    void animate(float frameSeconds) { brickBoard.animate(frameSeconds); }

    const BrickBoard& board() const { return brickBoard; }
    PhysicsBackend& backend() { return *physics; }
    const SimulationStats& stats() const { return statistics; }
    const SimulationTuning& tuning() const { return settings; }
    bool isCleared() const { return brickBoard.aliveBrickCount() == 0; }
    float launchX() const { return static_cast<float>(brickBoard.columns()) * 0.5f; }
    float launchY() const { return static_cast<float>(brickBoard.rows()) - settings.ballRadius * 2.0f; }

private:
    StepParams stepParams() const;
    void syncBoardIfChanged();
    void resolveHits();

    SimulationTuning settings;
    std::unique_ptr<PhysicsBackend> physics;
    BrickBoard brickBoard;
    TargetField targetField;
    uint64_t uploadedRevision = ~0ull;
    std::vector<BallSpawn> pendingSpawns;
    std::vector<BallHit> hits;
    std::vector<uint32_t> killedBalls;
    SimulationStats statistics;
    std::mt19937 random{20261026u};
    float accumulator = 0.0f;
};
