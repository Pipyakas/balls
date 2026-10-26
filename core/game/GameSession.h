#pragma once

#include "game/GameMode.h"
#include "game/LauncherQueue.h"
#include "game/SpawnController.h"
#include "level/LevelData.h"
#include "sim/BackendFactory.h"
#include "sim/Simulation.h"

#include <array>
#include <string>

enum class SessionState { Ready, Running, Won };

inline constexpr std::array<int, 6> spawnRateOptions{10, 50, 200, 1000, 2000, 0};
inline constexpr std::array<BallCount, 5> ballMultiplierOptions{1, 10, 100, 1000, 10000};
inline constexpr std::array<BallCount, 5> ballLimitOptions{10'000, 100'000, 1'000'000, 10'000'000, unlimitedBalls};
inline constexpr float normalModeSpawnRate = 60.0f;

class GameSession {
public:
    GameSession(GameMode mode, LevelData level, BackendKind backend);

    void restart();
    void reload(LevelData level);
    void update(float deltaTime);

    bool chooseLauncher(int column, int row);
    void startAutoRelease();
    void selectSpawnRate(int optionIndex);
    void selectBallMultiplier(int optionIndex);
    void selectBallLimit(int optionIndex);
    bool switchBackend(BackendKind kind);

    GameMode mode() const { return gameMode; }
    SessionState state() const { return sessionState; }
    const LevelData& level() const { return levelData; }
    Simulation& simulation() { return physicsSimulation; }
    const Simulation& simulation() const { return physicsSimulation; }
    const LauncherQueue& launchers() const { return launcherQueue; }
    const SpawnController& spawner() const { return spawnController; }
    BackendKind backend() const { return backendKind; }
    int spawnRateIndex() const { return selectedSpawnRate; }
    int ballMultiplierIndex() const { return selectedMultiplier; }
    int ballLimitIndex() const { return selectedBallLimit; }
    BallCount ballLimit() const { return ballLimitOptions[selectedBallLimit]; }
    bool isAutoReleasing() const { return autoRelease; }
    double elapsedSeconds() const { return elapsed; }
    const std::string& lastBackendFault() const { return backendFault; }

private:
    void releaseLauncher(const Launcher& launcher);
    void recoverFromBackendFault();

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
    std::string backendFault;
};
