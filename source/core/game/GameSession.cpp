#include "game/GameSession.h"

#include "common/BallColor.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <utility>

namespace {

// A GPU backend can fail to build even when available: starting a level right after one held hundreds of millions of
// balls, the GPU memory is still being given back while the new session builds, and the target PC crashed loading the
// level into a missing backend. The session then starts on the fastest CPU backend instead.
BackendHandle firstBackendThatBuilds(BackendKind& kind, LoadProgress* progress) {
    for (const BackendKind candidate : {kind, fastestAvailableCpuBackend(), BackendKind::Scalar}) {
        if (!isBackendAvailable(candidate)) {
            continue;
        }
        beginLoadStep(progress, std::format("Starting {} physics", backendKindName(candidate)));
        if (BackendHandle handle = createBackend(candidate)) {
            kind = candidate;
            return handle;
        }
        setLoadDetail(progress, "did not start");
    }
    kind = BackendKind::Scalar;
    beginLoadStep(progress, std::format("Starting {} physics", backendKindName(kind)));
    return createBackend(BackendKind::Scalar);
}

std::string boardStepText(const BrickLayout& layout) {
    return std::format("Placing {} bricks on the {} x {} board", groupedCount(layout.types.size()),
                       groupedCount(static_cast<uint64_t>(layout.width)),
                       groupedCount(static_cast<uint64_t>(layout.playableHeight)));
}

// Performance auto release keeps this many launchers spawning at once, so every color is on the board together
// however many balls one launcher holds.
constexpr size_t concurrentPerformanceReleases = ballColorCount;

double secondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

}

GameSession::GameSession(GameMode mode, LevelData level, BackendKind backend, int ticksPerSecond,
                         LoadProgress* progress)
    : gameMode(mode), levelData(std::move(level)), backendKind(backend), physicsTicksPerSecond(ticksPerSecond),
      physicsSimulation(firstBackendThatBuilds(backendKind, progress), simulationTuning(levelData.rules)),
      additionGovernor(physicsSimulation.tuning().tickSeconds) {
    beginLoadStep(progress, boardStepText(levelData.bricks));
    restart(progress);
}

void GameSession::restart(LoadProgress* progress) {
    physicsSimulation.setTuning(simulationTuning(levelData.rules));
    // The Settings page's physics rate replaces the level's when the player chose one other than the default.
    if (physicsTicksPerSecond > 0 && physicsTicksPerSecond != defaultTicksPerSecond) {
        physicsSimulation.setTicksPerSecond(physicsTicksPerSecond);
    }
    physicsSimulation.load(levelData.bricks, progress);
    launcherQueue.load(levelData.launchers);
    spawnController.clear();
    spawnController.setInterleaved(gameMode == GameMode::Performance);
    fillRemaining = 0;
    additionGovernor = AdditionGovernor(physicsSimulation.tuning().tickSeconds);
    spawnController.setBallsPerSecond(gameMode == GameMode::Normal ? levelData.rules.normalSpawnRate
                                                                   : static_cast<float>(spawnRateOptions[selectedSpawnRate]));
    sessionState = gameMode == GameMode::Normal ? SessionState::Running : SessionState::Ready;
    autoRelease = false;
    elapsed = 0.0;
}

void GameSession::reload(LevelData level) {
    levelData = std::move(level);
    restart();
}

void GameSession::releaseLauncher(const Launcher& launcher) {
    const BallCount multiplier = gameMode == GameMode::Performance ? ballMultiplierOptions[selectedMultiplier] : 1;
    spawnController.enqueue(launcher.color, saturatingMultiply(static_cast<BallCount>(launcher.ballCount), multiplier));
}

bool GameSession::chooseLauncher(int column, int row) {
    if (gameMode != GameMode::Normal || sessionState != SessionState::Running) {
        return false;
    }
    const auto launcher = launcherQueue.take(column, row);
    if (!launcher) {
        return false;
    }
    releaseLauncher(*launcher);
    return true;
}

void GameSession::startAutoRelease() {
    if (gameMode == GameMode::Performance && sessionState == SessionState::Ready) {
        autoRelease = true;
        sessionState = SessionState::Running;
    }
}

void GameSession::fillToBallLimit() {
    if (gameMode != GameMode::Performance) {
        return;
    }
    useCapacityBallLimit();
    // Auto then ramps the balls up to that capacity, as fast as 60 FPS allows.
    if (sessionState == SessionState::Ready) {
        autoRelease = false;
        sessionState = SessionState::Running;
    }
    fillRemaining = unlimitedBalls;
}

void GameSession::cancelFill() {
    fillRemaining = 0;
}

void GameSession::reportFrameWorkSeconds(double workSeconds) {
    additionGovernor.recordFrame(workSeconds);
}

// Fill balls come out of the launch point like any released ball. Scattering them over the board put
// about one in six inside a brick, where they showed until the next steps pushed them out.
void GameSession::launchAcrossColors(BallCount count) {
    for (BallCount remaining = count; remaining > 0;) {
        const BallColor color = ballColorFromIndex(fillColorIndex % ballColorCount);
        fillColorIndex = (fillColorIndex + 1) % ballColorCount;
        const BallCount batch = std::min(remaining, static_cast<BallCount>(1 << 20));
        physicsSimulation.launchBalls(color, batch);
        remaining -= batch;
    }
}

void GameSession::fillWithBalls(BallCount count) {
    cancelFill();
    useMachineBallLimit(count);
    const BallCount live = physicsSimulation.liveBallCount();
    // Over frames, within the 60 FPS budget.
    fillRemaining = count > live ? count - live : 0;
}

void GameSession::selectSpawnRate(int optionIndex) {
    selectedSpawnRate = std::clamp(optionIndex, 0, static_cast<int>(spawnRateOptions.size()) - 1);
    if (gameMode == GameMode::Performance) {
        spawnController.setBallsPerSecond(static_cast<float>(spawnRateOptions[selectedSpawnRate]));
    }
}

// Auto is the most balls the backend holds, worked out again every launch and never saved: in free flight a frame
// costs the same whatever the ball count, and balls are only added as fast as 60 FPS allows.
void GameSession::useCapacityBallLimit() {
    cancelFill();
    measuredMachineLimit.reset();
    machineLimitSelected = true;
    machineLimitIsCapacity = true;
}

std::optional<BallCount> GameSession::machineBallLimit() const {
    if (machineLimitIsCapacity) {
        return static_cast<BallCount>(physicsSimulation.backend().ballCapacityLimit());
    }
    return measuredMachineLimit;
}

void GameSession::useMachineBallLimit(BallCount limit) {
    measuredMachineLimit = limit;
    machineLimitSelected = true;
    machineLimitIsCapacity = false;
    physicsSimulation.trimBallsTo(limit);
}

void GameSession::selectBallLimit(int optionIndex) {
    machineLimitSelected = false;
    machineLimitIsCapacity = false;
    selectedBallLimit = std::clamp(optionIndex, 0, static_cast<int>(ballLimitOptions.size()) - 1);
}

void GameSession::selectBallMultiplier(int optionIndex) {
    selectedMultiplier = std::clamp(optionIndex, 0, static_cast<int>(ballMultiplierOptions.size()) - 1);
}

GameSession::~GameSession() {
    if (pendingBuild) {
        pendingBuild->abandon();
    }
    BackendWorker::shared().retire(physicsSimulation.releaseBackend());
}

// A newer request replaces one still building, which also picks up a compute GPU chosen in between.
void GameSession::requestBackend(BackendKind kind) {
#if defined(BALLS_FIXED_TARGET)
    // The scoring build has one backend, built once per process; rebuilding it would only wait for itself.
    if (kind == backendKind && !pendingBuild) {
        return;
    }
#endif
    if (pendingBuild) {
        pendingBuild->abandon();
    }
    pendingBuild = BackendWorker::shared().build(kind);
    pendingBuildForCollisions = false;
}

BallCollisionsChange GameSession::setBallCollisions(bool enabled) {
    physicsSimulation.setBallCollisions(enabled);
    if (!enabled || physicsSimulation.ballCollisionsActive()) {
        return BallCollisionsChange::None;
    }
    if (!isBackendSelectable(BackendKind::Scalar)) {
        return BallCollisionsChange::ScalarNotOffered;
    }
    if (pendingBackend() != BackendKind::Scalar) {
        requestBackend(BackendKind::Scalar);
    }
    pendingBuildForCollisions = true;
    return BallCollisionsChange::SwitchingToScalar;
}

std::string_view ballCollisionsMessage(BallCollisionsChange change) {
    switch (change) {
    case BallCollisionsChange::SwitchingToScalar:
        return "Ball collisions need the Scalar backend - switching to Scalar";
    case BallCollisionsChange::ScalarNotOffered:
        return "Ball collisions need the Scalar backend, which is not offered on this machine";
    case BallCollisionsChange::None:
        break;
    }
    return {};
}

std::optional<BackendKind> GameSession::pendingBackend() const {
    return pendingBuild ? std::optional<BackendKind>(pendingBuild->kind()) : std::nullopt;
}

std::optional<BackendSwitchOutcome> GameSession::takeBackendSwitchOutcome() {
    return std::exchange(switchOutcome, std::nullopt);
}

void GameSession::pollBackgroundWork() {
    if (pendingBuild) {
        if (std::optional<BackendHandle> finished = pendingBuild->take()) {
            const BackendKind kind = pendingBuild->kind();
            const bool forCollisions = std::exchange(pendingBuildForCollisions, false);
            pendingBuild.reset();
            if (*finished) {
                adoptBackend(kind, std::move(*finished));
            } else {
                switchOutcome = BackendSwitchOutcome{kind, false};
            }
            switchOutcome->forBallCollisions = forCollisions;
        }
    }
    refillBatch();
}

void GameSession::adoptBackend(BackendKind kind, BackendHandle backend) {
    BackendReplacement replacement = physicsSimulation.replaceBackend(std::move(backend));
    BackendWorker::shared().retire(std::move(replacement.retired));
    backendKind = kind;
    // A refill still owed from an earlier switch carries over: those balls were never in the backend being replaced.
    refillRemaining += replacement.ballsToRefill;
    switchOutcome = BackendSwitchOutcome{kind, true};
}

// Balls that could not be moved to the new backend are launched into it instead, a batch per frame sized to stay
// within the budget.
void GameSession::refillBatch() {
    constexpr double budgetMs = 4.0;
    constexpr BallCount smallestBatch = BallCount{1} << 12;
    constexpr BallCount largestBatch = BallCount{1} << 24;
    if (refillRemaining == 0) {
        return;
    }
    // The ball limit caps a refill like any release: after a switch, Auto may have chosen a lower limit for the new
    // backend than the balls the old one held.
    const BallCount limit = gameMode == GameMode::Performance ? ballLimit() : unlimitedBalls;
    const BallCount room = remainingCapacity(limit, physicsSimulation.liveBallCount());
    if (room == 0) {
        refillRemaining = 0;
        return;
    }
    // A full backend may only be waiting for the retired one to free its device memory, which the backend worker does
    // just after the switch; the refill gives up only once the room has stayed at zero for a while.
    const auto now = std::chrono::steady_clock::now();
    if (physicsSimulation.ballRoom() == 0) {
        if (!refillStalledSince) {
            refillStalledSince = now;
        } else if (now - *refillStalledSince > refillPatience) {
            refillRemaining = 0;
            refillStalledSince.reset();
        }
        return;
    }
    refillStalledSince.reset();
    const BallCount batch = std::min({refillRemaining, refillBatchBalls, room});
    const auto start = now;
    launchAcrossColors(batch);
    const double elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    refillRemaining -= batch;
    const double scale = elapsedMs > 0.0 ? budgetMs / elapsedMs : 2.0;
    refillBatchBalls = std::clamp(static_cast<BallCount>(static_cast<double>(batch) * std::min(scale, 2.0)), smallestBatch,
                                  largestBatch);
}

// A backend that faulted cannot keep running while a replacement builds, so this one switch happens on the frame.
void GameSession::switchBackendNow(BackendKind kind) {
    if (BackendHandle backend = createBackend(kind)) {
        adoptBackend(kind, std::move(backend));
    }
}

void GameSession::update(float deltaTime) {
    launcherQueue.animate(deltaTime);
    physicsSimulation.animate(deltaTime);
    if (sessionState == SessionState::Running) {
        elapsed += deltaTime;
        while (autoRelease && spawnController.activeStreams() < concurrentPerformanceReleases) {
            const auto launcher = launcherQueue.takeNextInRotation();
            if (!launcher) {
                break;
            }
            releaseLauncher(*launcher);
        }
    }
    addBallsWithinBudget(deltaTime);
    if (sessionState != SessionState::Ready) {
        const auto stepStart = std::chrono::steady_clock::now();
        physicsSimulation.advance(deltaTime);
        additionGovernor.recordStep(secondsSince(stepStart), physicsSimulation.stats().ticksLastFrame,
                                    physicsSimulation.liveBallCount());
        recoverFromBackendFault();
    }
    // Performance mode has no win: it keeps releasing and spawning after the last brick until the player leaves.
    if (gameMode == GameMode::Normal && sessionState == SessionState::Running && physicsSimulation.isCleared()) {
        sessionState = SessionState::Won;
    }
}

namespace {

// Hands up to count balls to launch in slices, each sent to the backend before the next, and stops once the next
// slice would run past seconds after start, so a frame whose additions cost more than the governor expected stops
// short instead of running over. launch returns how many it launched; fewer ends the batch. Returns the total.
template <typename Launch>
BallCount launchInSlices(Simulation& simulation, BallCount count, double seconds,
                         std::chrono::steady_clock::time_point start, double secondsPerBall, Launch&& launch) {
    constexpr BallCount sliceBalls = BallCount{1} << 18;
    constexpr BallCount smallestSlice = BallCount{1} << 12;
    constexpr double sliceCostMargin = 1.25;
    simulation.flushSpawns();
    BallCount launched = 0;
    while (launched < count) {
        const double left = seconds - secondsSince(start);
        const BallCount fits = left > 0.0 ? static_cast<BallCount>(left / (secondsPerBall * sliceCostMargin)) : 0;
        const BallCount slice = std::min({count - launched, sliceBalls, fits});
        if (slice == 0 || (slice < smallestSlice && slice < count - launched)) {
            break;
        }
        const auto sliceStart = std::chrono::steady_clock::now();
        const BallCount sent = launch(slice);
        simulation.flushSpawns();
        if (sent >= smallestSlice) {
            secondsPerBall = secondsSince(sliceStart) / static_cast<double>(sent);
        }
        launched += sent;
        if (sent < slice) {
            break;
        }
    }
    return launched;
}

}

void GameSession::addBallsWithinBudget(float deltaTime) {
    const bool spawning = sessionState == SessionState::Running;
    if (!spawning && fillRemaining == 0) {
        return;
    }
    const auto start = std::chrono::steady_clock::now();
    const BallCount before = physicsSimulation.liveBallCount();
    const BallCount limit = gameMode == GameMode::Performance ? ballLimit() : unlimitedBalls;
    const bool freeFlight = physicsSimulation.isCleared() && !physicsSimulation.ballCollisionsActive();
    const BallCount allowance =
        std::min(additionGovernor.allowance(before, freeFlight), remainingCapacity(limit, before));
    const double additionSeconds = additionGovernor.additionSeconds(before, freeFlight);
    const double secondsPerBall = additionGovernor.predictedSeconds(1);
    BallCount used = 0;
    if (spawning) {
        const BallCount accrued = spawnController.accrue(deltaTime, allowance);
        used = launchInSlices(physicsSimulation, accrued, additionSeconds, start, secondsPerBall,
                              [&](BallCount slice) { return spawnController.release(physicsSimulation, slice); });
    }
    if (fillRemaining > 0) {
        const BallCount room =
            std::min(remainingCapacity(limit, physicsSimulation.liveBallCount()), physicsSimulation.ballRoom());
        if (room == 0) {
            fillRemaining = 0;
        } else {
            const BallCount batch = std::min({fillRemaining, room, allowance > used ? allowance - used : 0});
            fillRemaining -= launchInSlices(physicsSimulation, batch, additionSeconds, start, secondsPerBall,
                                            [&](BallCount slice) {
                                                launchAcrossColors(slice);
                                                return slice;
                                            });
        }
    }
    physicsSimulation.flushSpawns();
    const BallCount after = physicsSimulation.liveBallCount();
    additionGovernor.recordAdditions(secondsSince(start), after > before ? after - before : 0);
}

void GameSession::recoverFromBackendFault() {
    const std::string_view fault = physicsSimulation.backend().faultMessage();
    if (fault.empty()) {
        return;
    }
    backendFault = std::string(fault);
    switchBackendNow(fastestAvailableCpuBackend());
}
