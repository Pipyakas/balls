#pragma once

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

// The balls as the renderer draws them, copied out of the backend after a physics frame so the UI can draw them
// while the backend steps the next one.
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

// Everything the gameplay UI reads about a session, published by the physics thread after each of its frames. The
// UI holds one for a whole frame and never touches the session itself, so a physics step of any length cannot
// stall a UI frame.
struct SessionView {
    uint64_t frameNumber = 0;
    GameMode mode = GameMode::Normal;
    SessionState state = SessionState::Ready;
    double elapsedSeconds = 0.0;

    LauncherQueue launchers;
    BallCount pendingSpawn = 0;
    bool autoReleasing = false;
    int spawnRateIndex = 0;
    int ballMultiplierIndex = 0;
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
    float ballRadius = 0.2f;
    BallFrame balls;

    // How long the physics thread took for its last frame, and how many frames it completed per second. With the
    // threads apart the UI frame rate no longer says how fast the simulation runs.
    double physicsFrameMs = 0.0;
    double physicsFramesPerSecond = 0.0;
    double physicsLongestFrameMs = 0.0;
    long long physicsSlowFrames = 0;
};

// Fills everything in a view but the balls from the session as it is now, with its board copied, or shared with the
// previous view when it has not changed. The physics
// thread builds its views this way; a front end that ticks the session on its game thread uses it to read the session
// the same way, for the control API's state.
void describeSession(const GameSession& session, SessionView& view, const SessionView* previous = nullptr);
