#pragma once

#include "common/LoadProgress.h"
#include "game/AdditionGovernor.h"
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

inline constexpr std::array<int, 10> spawnRateOptions{10, 50, 200, 1000, 2000, 10'000, 50'000, 200'000, 1'000'000, 0};

enum class BallLimitChoice { Level, Auto, Max };
inline constexpr int ballLimitChoiceCount = 3;

struct BackendSwitchOutcome {
    BackendKind kind = BackendKind::Scalar;
    bool succeeded = false;

    bool forBallCollisions = false;
};

enum class BallCollisionsChange { None, SwitchingToScalar, ScalarNotOffered };

std::string_view ballCollisionsMessage(BallCollisionsChange change);

class GameSession {
public:

    GameSession(GameMode mode, LevelData level, BackendKind backend, int ticksPerSecond = defaultTicksPerSecond,
                LoadProgress* progress = nullptr);
    ~GameSession();
    GameSession(const GameSession&) = delete;
    GameSession& operator=(const GameSession&) = delete;

    void restart(LoadProgress* progress = nullptr);
    void reload(LevelData level);
    void update(float deltaTime);

    void pollBackgroundWork();

    bool chooseLauncher(int column, int row);
    void startAutoRelease();
    void selectSpawnRate(int optionIndex);

    void selectBallLimit(int optionIndex);

    void requestBackend(BackendKind kind);

    BallCollisionsChange setBallCollisions(bool enabled);
    std::optional<BackendKind> pendingBackend() const;

    std::optional<BackendSwitchOutcome> takeBackendSwitchOutcome();
    BallCount ballsLeftToRefill() const { return refillRemaining; }
    void fillToBallLimit();
    void cancelFill();
    void reportFrameWorkSeconds(double workSeconds);
    void useMachineBallLimit(BallCount limit);

    void useCapacityBallLimit();
    void fillWithBalls(BallCount count);
    bool isUsingMachineBallLimit() const { return limitChoice == BallLimitChoice::Auto; }

    bool releasesLevelCountsOnly() const { return limitChoice == BallLimitChoice::Level; }
    std::optional<BallCount> machineBallLimit() const;

    GameMode mode() const { return gameMode; }
    SessionState state() const { return sessionState; }
    const LevelData& level() const { return levelData; }
    Simulation& simulation() { return physicsSimulation; }
    const Simulation& simulation() const { return physicsSimulation; }
    const LauncherQueue& launchers() const { return launcherQueue; }
    const SpawnController& spawner() const { return spawnController; }
    BackendKind backend() const { return backendKind; }
    int spawnRateIndex() const { return selectedSpawnRate; }
    int ballLimitIndex() const { return static_cast<int>(limitChoice); }
    BallLimitChoice ballLimitChoice() const { return limitChoice; }
    bool streamsEndlessly() const { return endlessStreams; }
    BallCount ballLimit() const { return limitChoice == BallLimitChoice::Max ? unlimitedBalls : *machineBallLimit(); }
    bool isAutoReleasing() const { return autoRelease; }
    double elapsedSeconds() const { return elapsed; }
    const std::string& lastBackendFault() const { return backendFault; }

private:
    void setLimitChoice(BallLimitChoice choice);
    void launchAcrossColors(BallCount count);

    void addBallsWithinBudget(float deltaTime);
    void releaseLauncher(const Launcher& launcher);
    void startEndlessStreams();
    void recoverFromBackendFault();
    void switchBackendNow(BackendKind kind);
    void adoptBackend(BackendKind kind, BackendHandle backend);
    void refillBatch();

    GameMode gameMode;
    LevelData levelData;
    BackendKind backendKind;
    int physicsTicksPerSecond;
    Simulation physicsSimulation;
    AdditionGovernor additionGovernor;

    BallCount fillRemaining = 0;
    LauncherQueue launcherQueue;
    SpawnController spawnController;
    int slowFramesWhileProbing = 0;
    SessionState sessionState = SessionState::Ready;

    int selectedSpawnRate = static_cast<int>(spawnRateOptions.size()) - 1;
    BallLimitChoice limitChoice = BallLimitChoice::Level;
    bool autoRelease = false;

    bool endlessStreams = false;
    double elapsed = 0.0;
    int fillColorIndex = 0;

    std::optional<BallCount> measuredMachineLimit;
    std::string backendFault;
    std::shared_ptr<BackendBuild> pendingBuild;
    bool pendingBuildForCollisions = false;
    std::optional<BackendSwitchOutcome> switchOutcome;
    BallCount refillRemaining = 0;
    BallCount refillBatchBalls = BallCount{1} << 16;
    static constexpr std::chrono::seconds refillPatience{2};
    std::optional<std::chrono::steady_clock::time_point> refillStalledSince;
};
