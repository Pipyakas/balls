#include "game/GameSession.h"

#include "common/BallColor.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace {

double secondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

}

GameSession::GameSession(GameMode mode, LevelData level, BackendKind backend)
    : gameMode(mode), levelData(std::move(level)), backendKind(backend),
      physicsSimulation(createBackend(isBackendAvailable(backend) ? backend : fastestAvailableCpuBackend())),
      additionGovernor(physicsSimulation.tuning().tickSeconds) {
    if (!isBackendAvailable(backend)) {
        backendKind = fastestAvailableCpuBackend();
    }
    restart();
}

void GameSession::restart() {
    physicsSimulation.load(levelData.bricks);
    launcherQueue.load(levelData.launchers);
    spawnController.clear();
    pendingProbeBalls = 0;
    fillRemaining = 0;
    additionGovernor = AdditionGovernor(physicsSimulation.tuning().tickSeconds);
    spawnController.setBallsPerSecond(gameMode == GameMode::Normal ? normalModeSpawnRate
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

void GameSession::startCapacityProbe(double targetFps) {
    if (gameMode != GameMode::Performance) {
        return;
    }
#if defined(BALLS_FIXED_TARGET)
    (void)targetFps;
    useCapacityBallLimit();
    // Auto then ramps the balls up to that capacity like the probe did, as fast as 60 FPS allows.
    if (sessionState == SessionState::Ready) {
        autoRelease = false;
        sessionState = SessionState::Running;
    }
    fillRemaining = unlimitedBalls;
#else
    if (sessionState == SessionState::Ready) {
        autoRelease = false;
        sessionState = SessionState::Running;
    }
    probeColorIndex = 0;
    pendingProbeBalls = 0;
    probe.start(targetFps);
    pumpCapacityProbe();
#endif
}

void GameSession::cancelCapacityProbe() {
    pendingProbeBalls = 0;
    fillRemaining = 0;
    probe.cancel();
}

void GameSession::reportFrameSeconds(double frameSeconds, double workSeconds) {
    latestFrameSeconds = frameSeconds;
    latestWorkSeconds = workSeconds;
    additionGovernor.recordFrame(workSeconds);
    pumpCapacityProbe();
}

void GameSession::pumpCapacityProbe() {
    if (!probe.isActive() || gameMode != GameMode::Performance || latestFrameSeconds <= 0.0) {
        return;
    }
    // The probe measures a count of balls, so it waits (and its windows do not advance) until the last ball of its
    // previous jump is in, unless the frames collapse on the way: then the rest of the jump is dropped.
    if (pendingProbeBalls > 0) {
        if (probe.abandonsJump(latestFrameSeconds, physicsSimulation.liveBallCount())) {
            pendingProbeBalls = 0;
            if (const auto trim = probe.takeTrimRequest()) {
                physicsSimulation.trimBallsTo(*trim);
            }
            return;
        }
        if (!drainPendingProbeBalls()) {
            return;
        }
    }
    const BallCount addition = probe.update(latestFrameSeconds, latestWorkSeconds, physicsSimulation.liveBallCount(),
                                            physicsSimulation.ballRoom());
    if (const auto trim = probe.takeTrimRequest()) {
        physicsSimulation.trimBallsTo(*trim);
    }
    if (probe.phase() == ProbePhase::Done) {
        useMachineBallLimit(probe.bestPassingCount());
        return;
    }
    pendingProbeBalls += addition;
    drainPendingProbeBalls();
}

// About 65 ns a ball: a probe jump to hundreds of millions of balls in one call froze the whole game for ten
// seconds, so a frame launches only what fits in this budget and the rest follows on the next frames.
bool GameSession::drainPendingProbeBalls() {
    constexpr auto budget = std::chrono::milliseconds(4);
    constexpr BallCount slice = 1 << 16;
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (pendingProbeBalls > 0) {
        const BallCount batch = std::min(pendingProbeBalls, slice);
        launchAcrossColors(batch);
        pendingProbeBalls -= batch;
        if (std::chrono::steady_clock::now() >= deadline) {
            break;
        }
    }
    return pendingProbeBalls == 0;
}

// Probe and fill balls come out of the launch point like any released ball. Scattering them over the board put
// about one in six inside a brick, where they showed until the next steps pushed them out.
void GameSession::launchAcrossColors(BallCount count) {
    for (BallCount remaining = count; remaining > 0;) {
        const BallColor color = ballColorFromIndex(probeColorIndex % ballColorCount);
        probeColorIndex = (probeColorIndex + 1) % ballColorCount;
        const BallCount batch = std::min(remaining, static_cast<BallCount>(1 << 20));
        physicsSimulation.launchBalls(color, batch);
        remaining -= batch;
    }
}

void GameSession::fillWithBalls(BallCount count) {
    cancelCapacityProbe();
    useMachineBallLimit(count);
    const BallCount live = physicsSimulation.liveBallCount();
#if defined(BALLS_FIXED_TARGET)
    // Over frames, within the 60 FPS budget.
    fillRemaining = count > live ? count - live : 0;
#else
    launchAcrossColors(count > live ? count - live : 0);
#endif
}

void GameSession::selectSpawnRate(int optionIndex) {
    selectedSpawnRate = std::clamp(optionIndex, 0, static_cast<int>(spawnRateOptions.size()) - 1);
    if (gameMode == GameMode::Performance) {
        spawnController.setBallsPerSecond(static_cast<float>(spawnRateOptions[selectedSpawnRate]));
    }
}

// The scoring build allocates its whole memory budget up front, and in free flight a frame costs the same whatever
// the ball count, so the most balls it holds is that capacity: nothing is left to measure.
void GameSession::useCapacityBallLimit() {
    cancelCapacityProbe();
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
    // A probe measures one backend; carried over, it would count the switch as a slow frame and trim to nothing.
    probe.cancel();
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
        if (autoRelease && spawnController.isIdle()) {
            if (const auto launcher = launcherQueue.takeNextInRotation()) {
                releaseLauncher(*launcher);
            }
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
    if (spawning) {
        spawnController.update(deltaTime, physicsSimulation, allowance);
    }
    if (fillRemaining > 0) {
        const BallCount live = physicsSimulation.liveBallCount();
        const BallCount used = live > before ? live - before : 0;
        const BallCount room = std::min(remainingCapacity(limit, live), physicsSimulation.ballRoom());
        if (room == 0) {
            fillRemaining = 0;
        } else {
            // In slices handed to the backend one by one, so a frame whose additions cost more than the governor
            // expected stops short instead of running over.
            constexpr BallCount fillSliceBalls = BallCount{1} << 18;
            constexpr BallCount smallestFillSlice = BallCount{1} << 12;
            constexpr double sliceCostMargin = 1.25;
            const double fillSeconds = additionGovernor.additionSeconds(before, freeFlight);
            BallCount batch = std::min({fillRemaining, room, allowance > used ? allowance - used : 0});
            physicsSimulation.flushSpawns();
            double secondsPerBall = additionGovernor.predictedSeconds(1);
            while (batch > 0) {
                const double left = fillSeconds - secondsSince(start);
                const BallCount fits =
                    left > 0.0 ? static_cast<BallCount>(left / (secondsPerBall * sliceCostMargin)) : 0;
                const BallCount slice = std::min({batch, fillSliceBalls, fits});
                if (slice == 0 || (slice < smallestFillSlice && slice < batch)) {
                    break;
                }
                const auto sliceStart = std::chrono::steady_clock::now();
                launchAcrossColors(slice);
                physicsSimulation.flushSpawns();
                if (slice >= smallestFillSlice) {
                    secondsPerBall = secondsSince(sliceStart) / static_cast<double>(slice);
                }
                batch -= slice;
                fillRemaining -= slice;
            }
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
