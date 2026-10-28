#pragma once

#include "game/CapacityProbe.h"
#include "game/GameMode.h"
#include "game/LauncherQueue.h"
#include "game/SpawnController.h"
#include "game/SpawnPacer.h"
#include "level/LevelData.h"
#include "sim/BackendFactory.h"
#include "sim/BackendWorker.h"
#include "sim/Simulation.h"

#include <array>
#include <memory>
#include <optional>
#include <string>

enum class SessionState { Ready, Running, Won };

inline constexpr std::array<int, 6> spawnRateOptions{10, 50, 200, 1000, 2000, 0};
inline constexpr std::array<BallCount, 5> ballMultiplierOptions{1, 10, 100, 1000, 10000};
inline constexpr std::array<BallCount, 5> ballLimitOptions{10'000, 100'000, 1'000'000, 10'000'000, unlimitedBalls};
inline constexpr float normalModeSpawnRate = 60.0f;
inline constexpr int probeSlowFramesToDropBacklog = 6;

struct BackendSwitchOutcome {
    BackendKind kind = BackendKind::Scalar;
    bool succeeded = false;
};

class GameSession {
public:
    GameSession(GameMode mode, LevelData level, BackendKind backend, int ticksPerSecond = defaultTicksPerSecond);
    ~GameSession();
    GameSession(const GameSession&) = delete;
    GameSession& operator=(const GameSession&) = delete;

    void restart();
    void reload(LevelData level);
    void update(float deltaTime);
    // Runs every frame, paused or not: swaps in a backend that finished building and refills a batch of balls.
    void pollBackgroundWork();

    bool chooseLauncher(int column, int row);
    void startAutoRelease();
    void selectSpawnRate(int optionIndex);
    void selectBallMultiplier(int optionIndex);
    void selectBallLimit(int optionIndex);
    // The new backend is built off the frame; the session keeps running on the current one until it is ready.
    void requestBackend(BackendKind kind);
    std::optional<BackendKind> pendingBackend() const;
    // Once per finished request, so the caller can react to the switch or to its failure.
    std::optional<BackendSwitchOutcome> takeBackendSwitchOutcome();
    BallCount ballsLeftToRefill() const { return refillRemaining; }
    void startCapacityProbe(double targetFps = defaultProbeTargetFps);
    void cancelCapacityProbe();
    void reportFrameSeconds(double frameSeconds, double workSeconds);
    void useMachineBallLimit(BallCount limit);
    void fillWithBalls(BallCount count);
    bool isUsingMachineBallLimit() const { return machineLimitSelected; }
    std::optional<BallCount> machineBallLimit() const { return measuredMachineLimit; }

    GameMode mode() const { return gameMode; }
    SessionState state() const { return sessionState; }
    const LevelData& level() const { return levelData; }
    Simulation& simulation() { return physicsSimulation; }
    const Simulation& simulation() const { return physicsSimulation; }
    const LauncherQueue& launchers() const { return launcherQueue; }
    const SpawnController& spawner() const { return spawnController; }
    const SpawnPacer& spawnPacer() const { return pacer; }
    BallCount queuedLaunches() const { return launchBacklog; }
    BackendKind backend() const { return backendKind; }
    const CapacityProbe& capacityProbe() const { return probe; }
    int spawnRateIndex() const { return selectedSpawnRate; }
    int ballMultiplierIndex() const { return selectedMultiplier; }
    int ballLimitIndex() const { return selectedBallLimit; }
    BallCount ballLimit() const { return machineLimitSelected ? *measuredMachineLimit : ballLimitOptions[selectedBallLimit]; }
    bool isAutoReleasing() const { return autoRelease; }
    double elapsedSeconds() const { return elapsed; }
    const std::string& lastBackendFault() const { return backendFault; }

private:
    void launchAcrossColors(BallCount count);
    void releaseLauncher(const Launcher& launcher);
    void recoverFromBackendFault();
    void pumpCapacityProbe();
    void releasePacedSpawns(float deltaTime);
    void switchBackendNow(BackendKind kind);
    void adoptBackend(BackendKind kind, BackendHandle backend);
    void refillBatch();

    GameMode gameMode;
    LevelData levelData;
    BackendKind backendKind;
    Simulation physicsSimulation;
    LauncherQueue launcherQueue;
    SpawnController spawnController;
    SpawnPacer pacer;
    BallCount launchBacklog = 0;
    int slowFramesWhileProbing = 0;
    SessionState sessionState = SessionState::Ready;
    int selectedSpawnRate = 2;
    int selectedMultiplier = 0;
    int selectedBallLimit = static_cast<int>(ballLimitOptions.size()) - 1;
    bool autoRelease = false;
    double elapsed = 0.0;
    double latestFrameSeconds = 0.0;
    double latestWorkSeconds = 0.0;
    CapacityProbe probe;
    int probeColorIndex = 0;
    std::optional<BallCount> measuredMachineLimit;
    bool machineLimitSelected = false;
    std::string backendFault;
    std::shared_ptr<BackendBuild> pendingBuild;
    std::optional<BackendSwitchOutcome> switchOutcome;
    BallCount refillRemaining = 0;
    BallCount refillBatchBalls = BallCount{1} << 16;
};
