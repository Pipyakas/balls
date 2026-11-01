#pragma once

#include "common/LoadProgress.h"

#include "common/BallCount.h"
#include "sim/BrickBoard.h"
#include "sim/BackendHandle.h"
#include "sim/PhysicsBackend.h"
#include "sim/TargetField.h"

#include <memory>
#include <random>
#include <vector>

inline constexpr int defaultTicksPerSecond = GameplayRules{}.ticksPerSecond;

inline constexpr float longestCatchUpSeconds = 4.0f / defaultTicksPerSecond;

struct SimulationTuning {
    float ballRadius = GameplayRules{}.ballRadius;
    float ballSpeed = GameplayRules{}.ballSpeed;
    float tickSeconds = 1.0f / static_cast<float>(GameplayRules{}.ticksPerSecond);
    int maxTicksPerFrame = GameplayRules{}.maxTicksPerFrame;
    int retargetBounceThreshold = GameplayRules{}.retargetBounces;
    float launchHalfAngleDegrees = GameplayRules{}.launchHalfAngleDegrees;
};

SimulationTuning simulationTuning(const GameplayRules& rules);

struct BackendReplacement {
    BackendHandle retired;
    BallCount ballsToRefill = 0;
};

struct SimulationStats {
    BallTally ballsByColor{};
    BallCount ballCount = 0;
    double lastFramePhysicsMs = 0.0;
    int ticksLastFrame = 0;
    BallCount totalBallsSpawned = 0;
    BallCount totalHits = 0;

    double lastSyncMs = 0.0;

    double lastTargetFieldMs = 0.0;
    double lastBoardUploadMs = 0.0;
    double lastStepMs = 0.0;
    double lastDeviceHitsMs = 0.0;
    double lastHostHitsMs = 0.0;
    size_t lastHostHits = 0;
};

class Simulation {
public:
    static constexpr size_t spawnFlushBalls = size_t{1} << 22;

    static constexpr size_t migrationChunkBalls = size_t{1} << 20;

    explicit Simulation(BackendHandle backend, SimulationTuning tuning = {});
    ~Simulation();

    void load(const BrickLayout& layout, LoadProgress* progress = nullptr);
    BackendReplacement replaceBackend(BackendHandle backend, size_t chunkBalls = migrationChunkBalls);

    BackendHandle releaseBackend() { return std::move(physics); }
    void launchBalls(BallColor color, BallCount count);

    void spawnBalls(const std::vector<BallSpawn>& spawns);
    void scatterBalls(BallColor color, BallCount count);
    void trimBallsTo(BallCount limit);
    void advance(float frameSeconds);
    void tick(int substeps = 1);

    void flushSpawns();
    void animate(float frameSeconds) { brickBoard.animate(frameSeconds); }

    const BrickBoard& board() const { return brickBoard; }

    BrickBoard& board() { return brickBoard; }
    PhysicsBackend& backend() { return *physics; }
    const PhysicsBackend& backend() const { return *physics; }
    const SimulationStats& stats() const { return statistics; }
    const SimulationTuning& tuning() const { return settings; }
    void setTuning(const SimulationTuning& tuning) { settings = tuning; }
    BallCount liveBallCount() const { return physics->ballCount() + pendingSpawns.size(); }
    BallCount ballRoom() const;

    void setRoomPastCapacity(bool on) { roomPastCapacity = on; }
    bool isCleared() const { return brickBoard.aliveBrickCount() == 0; }
    float launchX() const { return static_cast<float>(brickBoard.columns()) * 0.5f; }
    float launchY() const { return static_cast<float>(brickBoard.rows()) - settings.ballRadius * 2.0f; }

    StepParams stepParams(int substeps = 1) const;

    void setBallCollisions(bool enabled);

    void setTicksPerSecond(int ticksPerSecond);
    bool ballCollisionsRequested() const { return ballCollisionsWanted; }
    bool ballCollisionsActive() const { return ballCollisionsWanted && physics->supportsBallCollisions(); }

private:
    void syncBoardIfChanged();
    void flushSpawnsIfLarge();
    void resolveHits();
    void applyDeviceHits();

    SimulationTuning settings;
    BackendHandle physics;
    std::vector<int32_t> uploadedBrickHitsLeft;
    std::vector<int32_t> hostHitBricks;
    std::vector<int32_t> brickHitUpdates;
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
    uint32_t elapsedTicks = 0;
    bool ballCollisionsWanted = false;
    bool roomPastCapacity = false;
};
