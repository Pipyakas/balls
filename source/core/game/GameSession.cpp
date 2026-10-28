#include "game/GameSession.h"

#include "common/BallColor.h"

#include <algorithm>
#include <chrono>

GameSession::GameSession(GameMode mode, LevelData level, BackendKind backend)
    : gameMode(mode), levelData(std::move(level)), backendKind(backend),
      physicsSimulation(createBackend(isBackendAvailable(backend) ? backend : BackendKind::Scalar)) {
    if (!isBackendAvailable(backend)) {
        backendKind = BackendKind::Scalar;
    }
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

bool GameSession::switchBackend(BackendKind kind) {
    auto backend = createBackend(kind);
    if (!backend) {
        return false;
    }
    physicsSimulation.replaceBackend(std::move(backend));
    backendKind = kind;
    return true;
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
    switchBackend(fastestAvailableCpuBackend());
}
