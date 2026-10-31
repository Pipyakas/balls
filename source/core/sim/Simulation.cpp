#include "sim/Simulation.h"

#include "common/ParallelChunks.h"

#include <algorithm>
#include <chrono>
#include <new>
#include <numbers>

namespace {

constexpr int maximumScatterAttempts = 8;

// A uniform float in [0, 1) from 24 bits of a hash.
float unitFromBits(uint64_t bits) {
    constexpr float unitPerStep = 1.0f / 16777216.0f;
    return static_cast<float>(bits & 0xFFFFFF) * unitPerStep;
}

// SplitMix64's finalizer: launch n's random bits are the hash of the call's seed and n, so a launch does not depend
// on which core generated it, and no generator state is seeded per chunk.
uint64_t mixBits(uint64_t value) {
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
    return value ^ (value >> 31);
}

// Taylor series to x^9 and x^10, which the compiler vectorizes where std::sin and std::cos stay one call per ball.
// Launch angles stay within +-90 degrees, where both are within 4e-6 (1e-8 at the default 55).
void launchSinCos(float angle, float& sine, float& cosine) {
    const float square = angle * angle;
    sine = angle * (1.0f + square * (-1.0f / 6.0f + square * (1.0f / 120.0f + square * (-1.0f / 5040.0f +
                                                                                        square * (1.0f / 362880.0f)))));
    cosine = 1.0f + square * (-0.5f + square * (1.0f / 24.0f + square * (-1.0f / 720.0f + square * (1.0f / 40320.0f +
                                                                                       square * (-1.0f / 3628800.0f)))));
}

}

SimulationTuning simulationTuning(const GameplayRules& rules) {
    return {rules.ballRadius,
            rules.ballSpeed,
            1.0f / static_cast<float>(rules.ticksPerSecond),
            rules.maxTicksPerFrame,
            rules.retargetBounces,
            rules.launchHalfAngleDegrees};
}

Simulation::Simulation(BackendHandle backend, SimulationTuning tuning)
    : settings(tuning), physics(std::move(backend)) {}

Simulation::~Simulation() = default;

void Simulation::load(const BrickLayout& layout, LoadProgress* progress) {
    brickBoard.load(layout);
    beginLoadStep(progress, "Building the brick targets and sending the board to the physics");
    physics->clear();
    pendingSpawns.clear();
    statistics = {};
    accumulator = 0.0f;
    uploadedRevision = ~0ull;
    targetField.discardPendingRebuilds();
    targetsUploadPending = true;
    hitsUploadPending = true;
    syncBoardIfChanged();
}

// Runs on the physics thread, where it only delays the next tick: the UI keeps drawing the last published frame. Every
// ball is streamed across with its position and velocity, so the new backend continues the same simulation. Only
// when the new backend cannot hold them all are the balls it does not have handed back to be launched again: when it
// says so up front, or when partway through it runs out of host memory, refuses a chunk or faults, as the Vulkan
// backend does when device memory runs out. What it holds is counted, not what was sent.
BackendReplacement Simulation::replaceBackend(BackendHandle backend, size_t chunkBalls) {
    BackendReplacement replacement{std::move(physics), 0};
    physics = std::move(backend);
    uploadedRevision = ~0ull;
    targetsUploadPending = true;
    hitsUploadPending = true;
    syncBoardIfChanged();
    physics->setBallCollisions(ballCollisionsWanted);
    PhysicsBackend& previous = *replacement.retired;
    const size_t total = previous.ballCount();
    if (total > physics->ballCapacityLimit()) {
        replacement.ballsToRefill = total;
        return replacement;
    }
    while (previous.ballCount() > 0 && physics->faultMessage().empty()) {
        const std::vector<BallSpawn> chunk = previous.takeTail(std::min(previous.ballCount(), chunkBalls));
        if (chunk.empty()) {
            break;
        }
        const size_t expected = physics->ballCount() + chunk.size();
        try {
            physics->addBalls(chunk);
        } catch (const std::bad_alloc&) {
            break;
        }
        if (physics->ballCount() != expected) {
            break;
        }
    }
    replacement.ballsToRefill = total - std::min(physics->ballCount(), total);
    return replacement;
}

void Simulation::setTicksPerSecond(int ticksPerSecond) {
    const int rate = std::max(ticksPerSecond, 1);
    settings.tickSeconds = 1.0f / static_cast<float>(rate);
    settings.maxTicksPerFrame = std::max(1, static_cast<int>(std::ceil(longestCatchUpSeconds * static_cast<float>(rate))));
    accumulator = 0.0f;
}

void Simulation::setBallCollisions(bool enabled) {
    ballCollisionsWanted = enabled;
    physics->setBallCollisions(enabled);
}

// Launches are generated a chunk per core, each ball from a hash of this call's draw and its place, so the Auto ramp's
// millions a frame do not hold up the frame and the balls do not depend on the core count.
void Simulation::launchBalls(BallColor color, BallCount requested) {
    constexpr float degreesToRadians = std::numbers::pi_v<float> / 180.0f;
    constexpr size_t launchChunkBalls = size_t{1} << 12;
    constexpr uint64_t launchStride = 0x9E3779B97F4A7C15ull;
    const BallCount count = std::min(requested, ballRoom());
    const float halfAngle = settings.launchHalfAngleDegrees * degreesToRadians;
    const float originX = launchX();
    const float originY = launchY();
    const float radius = settings.ballRadius;
    const float speed = settings.ballSpeed;
    const int colorIndex = toIndex(color);
    const uint64_t seed = (static_cast<uint64_t>(random()) << 32) | random();
    for (BallCount launched = 0; launched < count;) {
        const size_t batch = static_cast<size_t>(std::min<BallCount>(count - launched, spawnFlushBalls));
        const size_t first = pendingSpawns.size();
        pendingSpawns.resize(first + batch);
        BallSpawn* const spawns = pendingSpawns.data() + first;
        const uint64_t batchStart = launched;
        parallelChunks(batch, launchChunkBalls, [&](size_t, size_t begin, size_t end) {
            for (size_t i = begin; i < end; ++i) {
                const uint64_t bits = mixBits(seed + (batchStart + i) * launchStride);
                const float angle = (2.0f * unitFromBits(bits) - 1.0f) * halfAngle;
                const float jitter = (2.0f * unitFromBits(bits >> 24) - 1.0f) * radius;
                float sine = 0.0f;
                float cosine = 0.0f;
                launchSinCos(angle, sine, cosine);
                spawns[i] = {originX + jitter, originY, sine * speed, -cosine * speed, colorIndex};
            }
        });
        launched += batch;
        flushSpawnsIfLarge();
    }
    statistics.ballsByColor[toIndex(color)] += count;
    statistics.totalBallsSpawned += count;
}

void Simulation::spawnBalls(const std::vector<BallSpawn>& spawns) {
    pendingSpawns.insert(pendingSpawns.end(), spawns.begin(), spawns.end());
}

BallCount Simulation::ballRoom() const {
    const BallCount limit = static_cast<BallCount>(physics->ballCapacityLimit());
    const BallCount live = liveBallCount();
    return limit > live ? limit - live : 0;
}

// Spawns are 20 bytes against 8 stored, so a fill step of billions of balls hands them to the backend in batches
// instead of holding them all.
void Simulation::flushSpawnsIfLarge() {
    if (pendingSpawns.size() >= spawnFlushBalls) {
        physics->addBalls(pendingSpawns);
        pendingSpawns.clear();
    }
}

void Simulation::scatterBalls(BallColor color, BallCount requested) {
    const BallCount count = std::min(requested, ballRoom());
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    const float width = static_cast<float>(brickBoard.columns());
    const float height = static_cast<float>(brickBoard.rows());
    pendingSpawns.reserve(pendingSpawns.size() + std::min(static_cast<size_t>(count), spawnFlushBalls));
    for (BallCount i = 0; i < count; ++i) {
        float x = 0.0f;
        float y = 0.0f;
        for (int attempt = 0; attempt < maximumScatterAttempts; ++attempt) {
            x = settings.ballRadius + unit(random) * (width - 2.0f * settings.ballRadius);
            y = settings.ballRadius + unit(random) * (height - 2.0f * settings.ballRadius);
            if (brickBoard.brickAt(static_cast<int>(x), static_cast<int>(y)) < 0) {
                break;
            }
        }
        const float angle = unit(random) * 2.0f * std::numbers::pi_v<float>;
        pendingSpawns.push_back({x, y, std::cos(angle) * settings.ballSpeed, std::sin(angle) * settings.ballSpeed,
                                 toIndex(color)});
        flushSpawnsIfLarge();
    }
    statistics.ballsByColor[toIndex(color)] += count;
    statistics.totalBallsSpawned += count;
}

void Simulation::trimBallsTo(BallCount limit) {
    flushSpawns();
    if (physics->ballCount() <= limit) {
        return;
    }
    const BallTally removed = physics->truncate(static_cast<size_t>(limit));
    for (int color = 0; color < ballColorCount; ++color) {
        statistics.ballsByColor[color] -= std::min(statistics.ballsByColor[color], removed[color]);
    }
    statistics.ballCount = liveBallCount();
}

StepParams Simulation::stepParams(int substeps) const {
    return {settings.tickSeconds,
            settings.ballRadius,
            settings.ballSpeed,
            static_cast<float>(brickBoard.columns()),
            static_cast<float>(brickBoard.rows()),
            brickBoard.paddedWidth(),
            brickBoard.paddedCellCount(),
            settings.retargetBounceThreshold,
            renderPackScale(static_cast<float>(brickBoard.columns())),
            renderPackScale(static_cast<float>(brickBoard.rows())),
            substeps,
            elapsedTicks,
            isCleared() && !ballCollisionsActive() ? 1 : 0};
}

void Simulation::syncBoardIfChanged() {
    using Clock = std::chrono::steady_clock;
    const auto milliseconds = [](Clock::time_point from, Clock::time_point to) {
        return std::chrono::duration<double, std::milli>(to - from).count();
    };
    const Clock::time_point fieldStart = Clock::now();
    statistics.lastTargetFieldMs = 0.0;
    statistics.lastBoardUploadMs = 0.0;
    const bool rebuilt = targetField.takeFinishedRebuild(brickBoard);
    bool sparseTargets = rebuilt && !targetsUploadPending && targetField.lastRebuildIsSparse() &&
                         physics->acceptsSparseBoardUpdates();
    targetsUploadPending = rebuilt || targetsUploadPending;
    const bool layoutChanged = uploadedRevision != brickBoard.revision();
    if (!layoutChanged && !hostHitsPending && !targetsUploadPending) {
        return;
    }
    if (layoutChanged) {
        if (targetField.requestRebuild(brickBoard)) {
            targetsUploadPending = true;
            sparseTargets = false;
        }
    }
    statistics.lastTargetFieldMs = milliseconds(fieldStart, Clock::now());
    const bool onlyChangedCells = uploadedRevision != ~0ull;
    // Hybrid's CPU side reports its hits to the host every tick; on a board of millions of bricks re-sending every
    // brick's count each time cost tens of milliseconds, so only the bricks hit are sent where the backend takes that.
    constexpr size_t wholeUploadDivisor = 8;
    const bool sendHitUpdates = hostHitsPending && !hitsUploadPending && physics->acceptsSparseBoardUpdates() &&
                                hostHitBricks.size() <= brickBoard.bricks().size() / wholeUploadDivisor;
    const bool sendHits = (hostHitsPending || hitsUploadPending) && !sendHitUpdates;
    brickHitUpdates.clear();
    if (sendHitUpdates) {
        std::ranges::sort(hostHitBricks);
        const auto [first, last] = std::ranges::unique(hostHitBricks);
        hostHitBricks.erase(first, last);
        for (const int32_t brick : hostHitBricks) {
            brickHitUpdates.push_back(brick);
            brickHitUpdates.push_back(std::max(brickBoard.bricks()[brick].hitsLeft, 0));
        }
    }
    hostHitBricks.clear();
    if (sendHits) {
        uploadedBrickHitsLeft = brickBoard.hitsLeftPerBrick();
    }
    hostHitsPending = false;
    hitsUploadPending = false;
    const std::span<const int32_t> hitsLeft =
        sendHits ? std::span<const int32_t>(uploadedBrickHitsLeft) : std::span<const int32_t>{};
    const std::span<const int32_t> targets =
        targetsUploadPending && !sparseTargets ? std::span<const int32_t>(targetField.targetCells())
                                               : std::span<const int32_t>{};
    const std::span<const int32_t> targetUpdates =
        targetsUploadPending && sparseTargets ? targetField.lastRebuildChanges() : std::span<const int32_t>{};
    const Clock::time_point uploadStart = Clock::now();
    physics->uploadBoard({brickBoard.columns(), brickBoard.rows(), brickBoard.paddedCells(), brickBoard.brickColors(),
                          targets, brickBoard.nearBrickFlags(), hitsLeft, brickBoard.changedCells(), onlyChangedCells,
                          brickHitUpdates, targetUpdates});
    statistics.lastBoardUploadMs = milliseconds(uploadStart, Clock::now());
    brickBoard.clearChangedCells();
    targetsUploadPending = false;
    uploadedRevision = brickBoard.revision();
}

void Simulation::applyDeviceHits() {
    const DeviceHitResults results = physics->deviceHitResults();
    bool anyDestroyed = false;
    for (const uint32_t brick : results.hitBricks) {
        const int index = static_cast<int>(brick);
        const bool wasAlive = brickBoard.bricks()[index].isAlive();
        brickBoard.applyHit(index);
        anyDestroyed = anyDestroyed || (wasAlive && !brickBoard.bricks()[index].isAlive());
    }
    for (size_t brick = 0; brick < results.brickHitsLeft.size(); ++brick) {
        const int index = static_cast<int>(brick);
        const bool wasAlive = brickBoard.bricks()[index].isAlive();
        brickBoard.applyHitsDownTo(index, results.brickHitsLeft[brick]);
        anyDestroyed = anyDestroyed || (wasAlive && !brickBoard.bricks()[index].isAlive());
    }
    for (int color = 0; color < ballColorCount; ++color) {
        statistics.ballsByColor[color] -= std::min(statistics.ballsByColor[color], results.killsByColor[color]);
    }
    statistics.totalHits += results.killed;
    if (anyDestroyed) {
        brickBoard.settleColumns();
    }
}

void Simulation::resolveHits() {
    killedBalls.clear();
    bool anyDestroyed = false;
    for (const BallHit& hit : hits) {
        const bool wasAlive = brickBoard.bricks()[hit.brick].isAlive();
        if (!wasAlive) {
            continue;
        }
        brickBoard.applyHit(hit.brick);
        anyDestroyed = anyDestroyed || !brickBoard.bricks()[hit.brick].isAlive();
        killedBalls.push_back(hit.ball);
        if (physics->acceptsSparseBoardUpdates()) {
            hostHitBricks.push_back(static_cast<int32_t>(hit.brick));
        }
        --statistics.ballsByColor[toIndex(brickBoard.bricks()[hit.brick].color)];
    }
    statistics.totalHits += killedBalls.size();
    // Backends report hits in ball order, so this is usually a reversal rather than a sort.
    if (std::ranges::is_sorted(killedBalls)) {
        std::ranges::reverse(killedBalls);
    } else {
        std::ranges::sort(killedBalls, std::greater<>{});
    }
    physics->removeBalls(killedBalls);
    if (anyDestroyed) {
        brickBoard.settleColumns();
    }
}

void Simulation::flushSpawns() {
    if (!pendingSpawns.empty()) {
        physics->addBalls(pendingSpawns);
        pendingSpawns.clear();
    }
}

void Simulation::tick(int substeps) {
    using Clock = std::chrono::steady_clock;
    const auto milliseconds = [](Clock::time_point from, Clock::time_point to) {
        return std::chrono::duration<double, std::milli>(to - from).count();
    };
    flushSpawns();
    const Clock::time_point syncStart = Clock::now();
    syncBoardIfChanged();
    hits.clear();
    const Clock::time_point stepStart = Clock::now();
    physics->step(stepParams(substeps), hits);
    const Clock::time_point stepEnd = Clock::now();
    elapsedTicks += static_cast<uint32_t>(substeps);
    statistics.lastHostHits = hits.size();
    Clock::time_point deviceHitsEnd = stepEnd;
    if (physics->resolvesHitsOnDevice()) {
        applyDeviceHits();
        deviceHitsEnd = Clock::now();
        if (!hits.empty()) {
            resolveHits();
            hostHitsPending = !killedBalls.empty();
        }
    } else {
        resolveHits();
    }
    const Clock::time_point end = Clock::now();
    statistics.lastSyncMs = milliseconds(syncStart, stepStart);
    statistics.lastStepMs = milliseconds(stepStart, stepEnd);
    statistics.lastDeviceHitsMs = milliseconds(stepEnd, deviceHitsEnd);
    statistics.lastHostHitsMs = milliseconds(deviceHitsEnd, end);
}

void Simulation::advance(float frameSeconds) {
    const auto start = std::chrono::steady_clock::now();
    accumulator = std::min(accumulator + frameSeconds, settings.tickSeconds * settings.maxTicksPerFrame);
    const int ticks = static_cast<int>(accumulator / settings.tickSeconds);
    accumulator -= static_cast<float>(ticks) * settings.tickSeconds;
    if (ticks > 0) {
        tick(ticks);
    }
    statistics.ticksLastFrame = ticks;
    statistics.ballCount = liveBallCount();
    statistics.lastFramePhysicsMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
