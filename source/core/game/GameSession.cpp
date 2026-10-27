#include "game/GameSession.h"

#include "common/BallColor.h"

#include <algorithm>

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
    probe.start(targetFps);
    pumpCapacityProbe();
}

void GameSession::cancelCapacityProbe() { probe.cancel(); }

void GameSession::reportFrameSeconds(double frameSeconds, double workSeconds) {
    latestFrameSeconds = frameSeconds;
    latestWorkSeconds = workSeconds;
    pumpCapacityProbe();
}

void GameSession::pumpCapacityProbe() {
    if (!probe.isActive() || gameMode != GameMode::Performance || latestFrameSeconds <= 0.0) {
        return;
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
    scatterAcrossColors(addition);
}

void GameSession::scatterAcrossColors(BallCount count) {
    for (BallCount remaining = count; remaining > 0;) {
        const BallColor color = ballColorFromIndex(probeColorIndex % ballColorCount);
        probeColorIndex = (probeColorIndex + 1) % ballColorCount;
        const BallCount batch = std::min(remaining, static_cast<BallCount>(1 << 20));
        physicsSimulation.scatterBalls(color, batch);
        remaining -= batch;
    }
}

void GameSession::fillWithBalls(BallCount count) {
    probe.cancel();
    useMachineBallLimit(count);
    const BallCount live = physicsSimulation.liveBallCount();
    scatterAcrossColors(count > live ? count - live : 0);
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
        const BallCount limit = gameMode == GameMode::Performance ? ballLimit() : unlimitedBalls;
        spawnController.update(deltaTime, physicsSimulation, remainingCapacity(limit, physicsSimulation.liveBallCount()));
    }
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
