#pragma once

#include "common/BallCount.h"
#include "sim/BrickBoard.h"
#include "sim/BackendHandle.h"
#include "sim/PhysicsBackend.h"
#include "sim/TargetField.h"

#include <memory>
#include <random>
#include <vector>

// The physics side of GameplayRules, whose defaults it starts from.
struct SimulationTuning {
    float ballRadius = GameplayRules{}.ballRadius;
    float ballSpeed = GameplayRules{}.ballSpeed;
    float tickSeconds = 1.0f / static_cast<float>(GameplayRules{}.ticksPerSecond);
    int maxTicksPerFrame = GameplayRules{}.maxTicksPerFrame;
    int retargetBounceThreshold = GameplayRules{}.retargetBounces;
    float launchHalfAngleDegrees = GameplayRules{}.launchHalfAngleDegrees;
};

SimulationTuning simulationTuning(const GameplayRules& rules);

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
};

class Simulation {
public:
    static constexpr size_t spawnFlushBalls = size_t{1} << 22;
    // Balls move between backends in chunks of this many, as positions and velocities: 20 MB at a time however many
    // balls there are, instead of one array of every ball. Chunks this size keep each device copy short, which halved
    // the UI's longest frame during a 100M-ball switch against 4M-ball chunks.
    static constexpr size_t migrationChunkBalls = size_t{1} << 20;

    explicit Simulation(BackendHandle backend, SimulationTuning tuning = {});

    void load(const BrickLayout& layout);
    BackendReplacement replaceBackend(BackendHandle backend, size_t chunkBalls = migrationChunkBalls);
    // Hands the backend over to be destroyed off the frame; the simulation must not be used afterwards.
    BackendHandle releaseBackend() { return std::move(physics); }
    void launchBalls(BallColor color, BallCount count);
    // Exact spawns, no RNG: how the parity harness feeds the same balls to two implementations.
    void spawnBalls(const std::vector<BallSpawn>& spawns);
    void scatterBalls(BallColor color, BallCount count);
    void trimBallsTo(BallCount limit);
    void advance(float frameSeconds);
    void tick(int substeps = 1);
    // Hands launched balls to the backend now instead of at the next tick, so their cost lands where it is timed.
    void flushSpawns();
    void animate(float frameSeconds) { brickBoard.animate(frameSeconds); }

    const BrickBoard& board() const { return brickBoard; }
    // For hosts that render the board themselves and clear its dirty-cell list once they have consumed it.
    BrickBoard& board() { return brickBoard; }
    PhysicsBackend& backend() { return *physics; }
    const PhysicsBackend& backend() const { return *physics; }
    const SimulationStats& stats() const { return statistics; }
    const SimulationTuning& tuning() const { return settings; }
    void setTuning(const SimulationTuning& tuning) { settings = tuning; }
    BallCount liveBallCount() const { return physics->ballCount() + pendingSpawns.size(); }
    BallCount ballRoom() const;
    bool isCleared() const { return brickBoard.aliveBrickCount() == 0; }
    float launchX() const { return static_cast<float>(brickBoard.columns()) * 0.5f; }
    float launchY() const { return static_cast<float>(brickBoard.rows()) - settings.ballRadius * 2.0f; }

    StepParams stepParams(int substeps = 1) const;

    void setBallCollisions(bool enabled);
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
};
