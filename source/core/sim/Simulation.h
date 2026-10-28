#pragma once

#include "common/BallCount.h"
#include "sim/BrickBoard.h"
#include "sim/BackendHandle.h"
#include "sim/PhysicsBackend.h"
#include "sim/TargetField.h"

#include <memory>
#include <random>
#include <vector>

inline constexpr int defaultTicksPerSecond = 120;
// A slow frame catches up at most this much game time; past it the game slows down instead of stalling.
inline constexpr float longestCatchUpSeconds = 4.0f / defaultTicksPerSecond;

struct SimulationTuning {
    float ballRadius = 0.2f;
    float ballSpeed = 14.0f;
    float tickSeconds = 1.0f / defaultTicksPerSecond;
    int maxTicksPerFrame = 4;
    int retargetBounceThreshold = 3;
    float launchHalfAngleDegrees = 55.0f;
};

// What replaceBackend hands back: the backend it retired, to be destroyed off the frame, and how many balls could not
// be moved across and have to be launched into the new backend again.
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
    // Where the last tick's time went on the calling thread: sending board changes, the backend step, and applying
    // brick hits the device resolved and the ones the host resolves (balls of host-reporting backends).
    double lastSyncMs = 0.0;
    double lastStepMs = 0.0;
    double lastDeviceHitsMs = 0.0;
    double lastHostHitsMs = 0.0;
    size_t lastHostHits = 0;
};

class WorkerPool;

class Simulation {
public:
    static constexpr size_t spawnFlushBalls = size_t{1} << 22;
    // Balls move between backends in chunks of this many, as positions and velocities: 20 MB at a time however many
    // balls there are, instead of one array of every ball. Chunks this size keep each device copy short, which halved
    // the UI's longest frame during a 100M-ball switch against 4M-ball chunks.
    static constexpr size_t migrationChunkBalls = size_t{1} << 20;

    explicit Simulation(BackendHandle backend, SimulationTuning tuning = {});
    ~Simulation();

    void load(const BrickLayout& layout);
    BackendReplacement replaceBackend(BackendHandle backend, size_t chunkBalls = migrationChunkBalls);
    // Hands the backend over to be destroyed off the frame; the simulation must not be used afterwards.
    BackendHandle releaseBackend() { return std::move(physics); }
    void launchBalls(BallColor color, BallCount count);
    void scatterBalls(BallColor color, BallCount count);
    void trimBallsTo(BallCount limit);
    void flushPendingSpawns();
    void advance(float frameSeconds);
    void tick(int substeps = 1);
    void animate(float frameSeconds) { brickBoard.animate(frameSeconds); }

    const BrickBoard& board() const { return brickBoard; }
    PhysicsBackend& backend() { return *physics; }
    const PhysicsBackend& backend() const { return *physics; }
    const SimulationStats& stats() const { return statistics; }
    const SimulationTuning& tuning() const { return settings; }
    BallCount liveBallCount() const { return physics->ballCount() + pendingSpawns.size(); }
    BallCount ballRoom() const;
    bool isCleared() const { return brickBoard.aliveBrickCount() == 0; }
    float launchX() const { return static_cast<float>(brickBoard.columns()) * 0.5f; }
    float launchY() const { return static_cast<float>(brickBoard.rows()) - settings.ballRadius * 2.0f; }

    StepParams stepParams(int substeps = 1) const;

    void setBallCollisions(bool enabled);
    // Only before balls are added: a held free-flight epoch counts ticks of the old length.
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
    std::unique_ptr<WorkerPool> launchPool;
};
