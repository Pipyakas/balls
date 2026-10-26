#include "sim/Simulation.h"

#include <algorithm>
#include <chrono>
#include <numbers>

Simulation::Simulation(std::unique_ptr<PhysicsBackend> backend, SimulationTuning tuning)
    : settings(tuning), physics(std::move(backend)) {}

void Simulation::load(const BrickLayout& layout) {
    brickBoard.load(layout);
    physics->clear();
    pendingSpawns.clear();
    statistics = {};
    accumulator = 0.0f;
    uploadedRevision = ~0ull;
    syncBoardIfChanged();
}

void Simulation::replaceBackend(std::unique_ptr<PhysicsBackend> backend) {
    const std::vector<BallSpawn> balls = physics->exportBalls();
    physics = std::move(backend);
    uploadedRevision = ~0ull;
    syncBoardIfChanged();
    physics->addBalls(balls);
}

void Simulation::launchBalls(BallColor color, int count) {
    constexpr float degreesToRadians = std::numbers::pi_v<float> / 180.0f;
    const float halfAngle = settings.launchHalfAngleDegrees * degreesToRadians;
    std::uniform_real_distribution<float> angleDistribution(-halfAngle, halfAngle);
    std::uniform_real_distribution<float> jitterDistribution(-settings.ballRadius, settings.ballRadius);
    for (int i = 0; i < count; ++i) {
        const float angle = angleDistribution(random);
        pendingSpawns.push_back({launchX() + jitterDistribution(random), launchY(),
                                 std::sin(angle) * settings.ballSpeed, -std::cos(angle) * settings.ballSpeed,
                                 toIndex(color)});
    }
    statistics.ballsByColor[toIndex(color)] += count;
    statistics.totalBallsSpawned += static_cast<uint64_t>(count);
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
            substeps};
}

void Simulation::syncBoardIfChanged() {
    if (uploadedRevision == brickBoard.revision()) {
        return;
    }
    targetField.rebuild(brickBoard);
    physics->uploadBoard(brickBoard, targetField);
    uploadedRevision = brickBoard.revision();
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
    if (!pendingSpawns.empty()) {
        physics->addBalls(pendingSpawns);
        pendingSpawns.clear();
    }
    syncBoardIfChanged();
    hits.clear();
    physics->step(stepParams(substeps), hits);
    resolveHits();
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
    statistics.ballCount = physics->ballCount() + pendingSpawns.size();
    statistics.lastFramePhysicsMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
