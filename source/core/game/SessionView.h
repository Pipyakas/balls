#pragma once

#include "game/BoardSnapshots.h"
#include "game/GameMode.h"
#include "game/GameSession.h"
#include "game/LauncherQueue.h"
#include "sim/BrickBoard.h"
#include "sim/Simulation.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct BallFrame {
    std::vector<uint32_t> packed;
    float fieldWidth = 1.0f;
    float fieldHeight = 1.0f;
    bool visibilityGrid = false;
};

struct HybridSplit {
    BallCount cpuBalls = 0;
    BallCount totalBalls = 0;
    double cpuStepMs = 0.0;
    double gpuStepMs = 0.0;
};

struct SessionView {
    uint64_t frameNumber = 0;
    GameMode mode = GameMode::Normal;
    SessionState state = SessionState::Ready;
    double elapsedSeconds = 0.0;

    LauncherQueue launchers;
    BallCount pendingSpawn = 0;

    bool endlessSpawn = false;
    bool autoReleasing = false;
    int spawnRateIndex = 0;
    int ballLimitIndex = 0;
    BallCount ballLimit = 0;
    bool usingMachineLimit = false;
    std::optional<BallCount> machineLimit;

    BackendKind backend = BackendKind::Scalar;
    std::optional<BackendKind> pendingBackend;
    BallCount refilling = 0;
    std::string backendFault;
    double deviceStepMs = 0.0;
    std::optional<HybridSplit> hybrid;
    bool collisionsRequested = false;
    bool collisionsActive = false;

    SimulationStats stats;
    BallCount liveBalls = 0;
    std::shared_ptr<const BrickBoard> board;
    float launchX = 0.0f;
    float launchY = 0.0f;
    float ballRadius = GameplayRules{}.ballRadius;
    BallFrame balls;

    double physicsFrameMs = 0.0;
    double physicsFramesPerSecond = 0.0;

    double physicsTicksPerSecond = 0.0;
    double targetTicksPerSecond = 0.0;
    double physicsLongestFrameMs = 0.0;
    long long physicsSlowFrames = 0;

    double boardPublishMs = 0.0;
};

void describeSession(const GameSession& session, SessionView& view, const SessionView* previous = nullptr,
                     BoardSnapshots* boards = nullptr);
