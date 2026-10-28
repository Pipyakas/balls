#include "game/GameSession.h"

#include "common/BallColor.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace {

// A GPU backend can fail to build even when available: starting a level right after one held hundreds of millions of
// balls, the GPU memory is still being given back while the new session builds, and the target PC crashed loading the
// level into a missing backend. The session then starts on the fastest CPU backend instead.
BackendHandle firstBackendThatBuilds(BackendKind& kind) {
    for (const BackendKind candidate : {kind, fastestAvailableCpuBackend(), BackendKind::Scalar}) {
        if (!isBackendAvailable(candidate)) {
            continue;
        }
        if (BackendHandle handle = createBackend(candidate)) {
            kind = candidate;
            return handle;
        }
    }
    kind = BackendKind::Scalar;
    return createBackend(BackendKind::Scalar);
}

}

GameSession::GameSession(GameMode mode, LevelData level, BackendKind backend, int ticksPerSecond)
    : gameMode(mode), levelData(std::move(level)), backendKind(backend),
      physicsSimulation(firstBackendThatBuilds(backendKind)) {
    physicsSimulation.setTicksPerSecond(ticksPerSecond);
    restart();
}

void GameSession::restart() {
    physicsSimulation.load(levelData.bricks);
    launcherQueue.load(levelData.launchers);
    spawnController.clear();
    pacer.reset();
    launchBacklog = 0;
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
    if (sessionState == SessionState::Ready) {
        autoRelease = false;
        sessionState = SessionState::Running;
    }
    probeColorIndex = 0;
    probe.start(targetFps, 1.0 / physicsSimulation.tuning().tickSeconds);
    pumpCapacityProbe();
}

void GameSession::cancelCapacityProbe() {
    probe.cancel();
    launchBacklog = 0;
}

void GameSession::reportFrameSeconds(double frameSeconds, double workSeconds) {
    latestFrameSeconds = frameSeconds;
    latestWorkSeconds = workSeconds;
    pacer.reportFrame(frameSeconds, physicsSimulation.liveBallCount());
    pumpCapacityProbe();
}

// A probe step is queued and drains over many frames; once frames stay slower than the target while it drains, the
// rest would only overshoot, so it is dropped and the probe measures what is already on the board.
void GameSession::pumpCapacityProbe() {
    const bool slowFrame = latestFrameSeconds * probe.targetFps() > 1.0;
    slowFramesWhileProbing = probe.isActive() && launchBacklog > 0 && slowFrame ? slowFramesWhileProbing + 1 : 0;
    if (slowFramesWhileProbing >= probeSlowFramesToDropBacklog) {
        launchBacklog = 0;
        slowFramesWhileProbing = 0;
    }
    // The probe measures only once its last addition is all on the board.
    if (!probe.isActive() || gameMode != GameMode::Performance || latestFrameSeconds <= 0.0 || launchBacklog > 0) {
        return;
    }
    const BallCount addition = probe.update(latestFrameSeconds, latestWorkSeconds, physicsSimulation.liveBallCount(),
                                            physicsSimulation.ballRoom(), physicsSimulation.stats().ticksLastFrame);
    if (const auto trim = probe.takeTrimRequest()) {
        physicsSimulation.trimBallsTo(*trim);
    }
    if (probe.phase() == ProbePhase::Done) {
        useMachineBallLimit(probe.bestPassingCount());
        return;
    }
    launchBacklog += addition;
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
    probe.cancel();
    useMachineBallLimit(count);
    const BallCount live = physicsSimulation.liveBallCount();
    launchBacklog = count > live ? count - live : 0;
}

// Launchers, the probe and fills all go through the pacer, so the frame that releases a million balls costs about
// what the frame before it did.
void GameSession::releasePacedSpawns(float deltaTime) {
    using Clock = std::chrono::steady_clock;
    const Clock::time_point start = Clock::now();
    const BallCount allowance = pacer.allowance();
    const BallCount fromBacklog = std::min(launchBacklog, allowance);
    launchAcrossColors(fromBacklog);
    launchBacklog -= fromBacklog;
    if (physicsSimulation.ballRoom() == 0) {
        launchBacklog = 0;
    }
    BallCount launched = fromBacklog;
    if (sessionState == SessionState::Running) {
        const BallCount limit = gameMode == GameMode::Performance ? ballLimit() : unlimitedBalls;
        launched += spawnController.update(deltaTime, physicsSimulation,
                                           remainingCapacity(limit, physicsSimulation.liveBallCount()),
                                           allowance - fromBacklog);
    }
    physicsSimulation.flushPendingSpawns();
    pacer.recordSpawn(launched, std::chrono::duration<double>(Clock::now() - start).count());
}

void GameSession::selectSpawnRate(int optionIndex) {
    selectedSpawnRate = std::clamp(optionIndex, 0, static_cast<int>(spawnRateOptions.size()) - 1);
    if (gameMode == GameMode::Performance) {
        spawnController.setBallsPerSecond(static_cast<float>(spawnRateOptions[selectedSpawnRate]));
    }
}

void GameSession::useMachineBallLimit(BallCount limit) {
    measuredMachineLimit = limit;
    machineLimitSelected = true;
    physicsSimulation.trimBallsTo(limit);
}

void GameSession::selectBallLimit(int optionIndex) {
    machineLimitSelected = false;
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
    if (pendingBuild) {
        pendingBuild->abandon();
    }
    pendingBuild = BackendWorker::shared().build(kind);
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
            pendingBuild.reset();
            if (*finished) {
                adoptBackend(kind, std::move(*finished));
            } else {
                switchOutcome = BackendSwitchOutcome{kind, false};
            }
        }
    }
    refillBatch();
}

void GameSession::adoptBackend(BackendKind kind, BackendHandle backend) {
    BackendReplacement replacement = physicsSimulation.replaceBackend(std::move(backend));
    BackendWorker::shared().retire(std::move(replacement.retired));
    backendKind = kind;
    refillRemaining = replacement.ballsToRefill;
    switchOutcome = BackendSwitchOutcome{kind, true};
}

// Too many balls to copy across in one frame are launched into the new backend instead, a batch per frame sized
// to stay within the budget.
void GameSession::refillBatch() {
    constexpr double budgetMs = 4.0;
    constexpr BallCount smallestBatch = BallCount{1} << 12;
    constexpr BallCount largestBatch = BallCount{1} << 24;
    if (refillRemaining == 0) {
        return;
    }
    const BallCount batch = std::min(refillRemaining, refillBatchBalls);
    const auto start = std::chrono::steady_clock::now();
    launchAcrossColors(batch);
    const double elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    refillRemaining -= batch;
    if (physicsSimulation.ballRoom() == 0) {
        refillRemaining = 0;
    }
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
    releasePacedSpawns(deltaTime);
    if (sessionState != SessionState::Ready) {
        physicsSimulation.advance(deltaTime);
        recoverFromBackendFault();
    }
    if (sessionState == SessionState::Running && physicsSimulation.isCleared()) {
        sessionState = SessionState::Won;
    }
}

void GameSession::recoverFromBackendFault() {
    const std::string_view fault = physicsSimulation.backend().faultMessage();
    if (fault.empty()) {
        return;
    }
    backendFault = std::string(fault);
    switchBackendNow(fastestAvailableCpuBackend());
}
