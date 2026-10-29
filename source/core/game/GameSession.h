#pragma once

#include "game/CapacityProbe.h"
#include "game/GameMode.h"
#include "game/LauncherQueue.h"
#include "game/SpawnController.h"
#include "level/LevelData.h"
#include "sim/BackendFactory.h"
#include "sim/BackendWorker.h"
#include "sim/Simulation.h"

#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

enum class SessionState { Ready, Running, Won };

// The game's five rates (10 to 2000), then faster ones: at 2000 a x10000 launcher takes minutes to empty. 0 is Max.
inline constexpr std::array<int, 10> spawnRateOptions{10, 50, 200, 1000, 2000, 10'000, 50'000, 200'000, 1'000'000, 0};
inline constexpr std::array<BallCount, 5> ballMultiplierOptions{1, 10, 100, 1000, 10000};
inline constexpr std::array<BallCount, 5> ballLimitOptions{10'000, 100'000, 1'000'000, 10'000'000, unlimitedBalls};
inline constexpr float normalModeSpawnRate = 60.0f;

struct BackendSwitchOutcome {
    BackendKind kind = BackendKind::Scalar;
    bool succeeded = false;
    // Set when the switch was the Scalar move that ball collisions asked for, not a pick from the Physics list.
    bool forBallCollisions = false;
};

// What turning ball collisions on did to the session. Only the Scalar backend implements them, and the design offers
// Scalar only where no other backend is available, so On either moves the game to Scalar or cannot take effect.
enum class BallCollisionsChange { None, SwitchingToScalar, ScalarNotOffered };

// The toast the design gives for the change, the same in every front end; empty for None.
std::string_view ballCollisionsMessage(BallCollisionsChange change);

class GameSession {
public:
    GameSession(GameMode mode, LevelData level, BackendKind backend);
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
    // Applies the Ball collisions setting, requesting Scalar when collisions need it and it is offered. The switch
    // lands like any other and comes back through takeBackendSwitchOutcome with forBallCollisions set.
    BallCollisionsChange setBallCollisions(bool enabled);
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
    // Hands the probe's pending balls to the simulation until this frame's time budget is spent; true once none are left.
    bool drainPendingProbeBalls();
    void releaseLauncher(const Launcher& launcher);
    void recoverFromBackendFault();
    void pumpCapacityProbe();
    void switchBackendNow(BackendKind kind);
    void adoptBackend(BackendKind kind, BackendHandle backend);
    void refillBatch();

    GameMode gameMode;
    LevelData levelData;
    BackendKind backendKind;
    Simulation physicsSimulation;
    LauncherQueue launcherQueue;
    SpawnController spawnController;
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
    // Balls the probe asked for that have not been launched yet; a big jump is spread over frames, not one stall.
    BallCount pendingProbeBalls = 0;
    std::optional<BallCount> measuredMachineLimit;
    bool machineLimitSelected = false;
    std::string backendFault;
    std::shared_ptr<BackendBuild> pendingBuild;
    bool pendingBuildForCollisions = false;
    std::optional<BackendSwitchOutcome> switchOutcome;
    BallCount refillRemaining = 0;
    BallCount refillBatchBalls = BallCount{1} << 16;
    static constexpr std::chrono::seconds refillPatience{2};
    std::optional<std::chrono::steady_clock::time_point> refillStalledSince;
};
