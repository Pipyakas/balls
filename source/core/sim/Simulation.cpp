#include "sim/Simulation.h"

#include <algorithm>
#include <chrono>
#include <numbers>

namespace {

constexpr int maximumScatterAttempts = 8;

}

Simulation::Simulation(BackendHandle backend, SimulationTuning tuning)
    : settings(tuning), physics(std::move(backend)) {}

void Simulation::load(const BrickLayout& layout) {
    brickBoard.load(layout);
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

void Simulation::replaceBackend(BackendHandle backend) {
    const std::vector<BallSpawn> balls = physics->exportBalls();
    physics = std::move(backend);
    uploadedRevision = ~0ull;
    targetsUploadPending = true;
    hitsUploadPending = true;
    syncBoardIfChanged();
    physics->addBalls(balls);
    physics->setBallCollisions(ballCollisionsWanted);
}

void Simulation::setBallCollisions(bool enabled) {
    ballCollisionsWanted = enabled;
    physics->setBallCollisions(enabled);
}

void Simulation::launchBalls(BallColor color, BallCount requested) {
    constexpr float degreesToRadians = std::numbers::pi_v<float> / 180.0f;
    const BallCount count = std::min(requested, ballRoom());
    const float halfAngle = settings.launchHalfAngleDegrees * degreesToRadians;
    std::uniform_real_distribution<float> angleDistribution(-halfAngle, halfAngle);
    std::uniform_real_distribution<float> jitterDistribution(-settings.ballRadius, settings.ballRadius);
    pendingSpawns.reserve(pendingSpawns.size() + std::min(static_cast<size_t>(count), spawnFlushBalls));
    for (BallCount i = 0; i < count; ++i) {
        const float angle = angleDistribution(random);
        pendingSpawns.push_back({launchX() + jitterDistribution(random), launchY(),
                                 std::sin(angle) * settings.ballSpeed, -std::cos(angle) * settings.ballSpeed,
                                 toIndex(color)});
        flushSpawnsIfLarge();
    }
    statistics.ballsByColor[toIndex(color)] += count;
    statistics.totalBallsSpawned += count;
}

BallCount Simulation::ballRoom() const {
    const BallCount limit = static_cast<BallCount>(physics->ballCapacityLimit());
    const BallCount live = liveBallCount();
    return limit > live ? limit - live : 0;
}

// Spawns are 20 bytes against 8 stored, so a probe step of billions of balls hands them to the backend in batches
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

void Simulation::flushPendingSpawns() {
    if (!pendingSpawns.empty()) {
        physics->addBalls(pendingSpawns);
        pendingSpawns.clear();
    }
}

void Simulation::trimBallsTo(BallCount limit) {
    flushPendingSpawns();
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
    targetsUploadPending = targetField.takeFinishedRebuild(brickBoard) || targetsUploadPending;
    const bool layoutChanged = uploadedRevision != brickBoard.revision();
    if (!layoutChanged && !hostHitsPending && !targetsUploadPending) {
        return;
    }
    if (layoutChanged) {
        targetsUploadPending = targetField.requestRebuild(brickBoard) || targetsUploadPending;
    }
    const bool onlyChangedCells = uploadedRevision != ~0ull;
    const bool sendHits = hostHitsPending || hitsUploadPending;
    if (sendHits) {
        uploadedBrickHitsLeft = brickBoard.hitsLeftPerBrick();
    }
    hostHitsPending = false;
    hitsUploadPending = false;
    const std::span<const int32_t> hitsLeft =
        sendHits ? std::span<const int32_t>(uploadedBrickHitsLeft) : std::span<const int32_t>{};
    const std::span<const int32_t> targets =
        targetsUploadPending ? std::span<const int32_t>(targetField.targetCells()) : std::span<const int32_t>{};
    physics->uploadBoard({brickBoard.columns(), brickBoard.rows(), brickBoard.paddedCells(), brickBoard.brickColors(),
                          targets, brickBoard.nearBrickFlags(), hitsLeft, brickBoard.changedCells(), onlyChangedCells});
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
        --statistics.ballsByColor[toIndex(brickBoard.bricks()[hit.brick].color)];
    }
    statistics.totalHits += killedBalls.size();
    std::ranges::sort(killedBalls, std::greater<>{});
    physics->removeBalls(killedBalls);
    if (anyDestroyed) {
        brickBoard.settleColumns();
    }
}

void Simulation::tick(int substeps) {
    flushPendingSpawns();
    syncBoardIfChanged();
    hits.clear();
    physics->step(stepParams(substeps), hits);
    elapsedTicks += static_cast<uint32_t>(substeps);
    if (physics->resolvesHitsOnDevice()) {
        applyDeviceHits();
        if (!hits.empty()) {
            resolveHits();
            hostHitsPending = !killedBalls.empty();
        }
    } else {
        resolveHits();
    }
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
