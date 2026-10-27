#pragma once

#include "common/BallCount.h"
#include "sim/BrickBoard.h"
#include "sim/BackendHandle.h"
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
    BallTally ballsByColor{};
    BallCount ballCount = 0;
    double lastFramePhysicsMs = 0.0;
    int ticksLastFrame = 0;
    BallCount totalBallsSpawned = 0;
    BallCount totalHits = 0;
};

class Simulation {
public:
    explicit Simulation(BackendHandle backend, SimulationTuning tuning = {});

    void load(const BrickLayout& layout);
    void replaceBackend(BackendHandle backend);
    void launchBalls(BallColor color, BallCount count);
    void scatterBalls(BallColor color, BallCount count);
    void trimBallsTo(BallCount limit);
    void advance(float frameSeconds);
    void tick(int substeps = 1);
    void animate(float frameSeconds) { brickBoard.animate(frameSeconds); }

    const BrickBoard& board() const { return brickBoard; }
    PhysicsBackend& backend() { return *physics; }
    const PhysicsBackend& backend() const { return *physics; }
    const SimulationStats& stats() const { return statistics; }
    const SimulationTuning& tuning() const { return settings; }
    BallCount liveBallCount() const { return physics->ballCount() + pendingSpawns.size(); }
    bool isCleared() const { return brickBoard.aliveBrickCount() == 0; }
    float launchX() const { return static_cast<float>(brickBoard.columns()) * 0.5f; }
    float launchY() const { return static_cast<float>(brickBoard.rows()) - settings.ballRadius * 2.0f; }

    StepParams stepParams(int substeps = 1) const;

    void setBallCollisions(bool enabled);
    bool ballCollisionsRequested() const { return ballCollisionsWanted; }
    bool ballCollisionsActive() const { return ballCollisionsWanted && physics->supportsBallCollisions(); }

private:
    void syncBoardIfChanged();
    void resolveHits();
    void applyDeviceHits();

    SimulationTuning settings;
    BackendHandle physics;
    std::vector<int32_t> uploadedBrickHitsLeft;
    BrickBoard brickBoard;
    TargetField targetField;
    uint64_t uploadedRevision = ~0ull;
    bool hostHitsPending = false;
    bool targetsUploadPending = true;
    bool hitsUploadPending = true;
    std::vector<BallSpawn> pendingSpawns;
    std::vector<BallHit> hits;
    std::vector<uint32_t> killedBalls;
    SimulationStats statistics;
    std::mt19937 random{20261026u};
    float accumulator = 0.0f;
    bool ballCollisionsWanted = false;
};
