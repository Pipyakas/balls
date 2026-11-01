#pragma once

#include "common/LoadProgress.h"
#include "game/GameSession.h"
#include "game/SessionView.h"

#include <array>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct PhysicsFrameInput {
    bool paused = false;

    double uiWorkSeconds = 0.0;

    float visibilitySlotCells = 0.0f;

    bool drawing = true;

    int framesPerSecond = 0;
};

enum class SessionEventKind { BackendSwitched, BackendFailed, BallCollisionsChanged };

struct SessionEvent {
    SessionEventKind kind = SessionEventKind::BackendSwitched;
    BackendKind backend = BackendKind::Scalar;

    bool forBallCollisions = false;
    BallCollisionsChange collisionsChange = BallCollisionsChange::None;
};

struct LevelLoadOutcome {
    bool loaded = false;
    std::string message;
};

class SessionRunner {
public:
    using Command = std::function<void(GameSession&)>;

    SessionRunner(GameMode mode, LevelData level, BackendKind backend, int ticksPerSecond = defaultTicksPerSecond);

    SessionRunner(GameMode mode, LevelFiles files, BackendKind backend, int ticksPerSecond,
                  std::shared_ptr<LoadProgress> progress);

    ~SessionRunner();
    SessionRunner(const SessionRunner&) = delete;
    SessionRunner& operator=(const SessionRunner&) = delete;

    void post(Command command);

    void setBallCollisions(bool enabled);

    void tick(const PhysicsFrameInput& input);

    std::shared_ptr<const SessionView> view() const;
    std::vector<SessionEvent> takeEvents();

    std::optional<LevelLoadOutcome> takeLoadOutcome();

    static bool waitForStoppedRunners(std::chrono::milliseconds timeout);

    struct Shared;

private:
    std::shared_ptr<Shared> shared;
};
