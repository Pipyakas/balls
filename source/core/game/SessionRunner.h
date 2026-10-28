#pragma once

#include "game/GameSession.h"
#include "game/SessionView.h"

#include <array>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

// What the UI hands the physics thread each frame.
struct PhysicsFrameInput {
    bool paused = false;
    // The UI's last frame, so the capacity probe judges the whole game and not the physics thread alone.
    double uiFrameSeconds = 0.0;
    double uiWorkSeconds = 0.0;
    // The size of one visibility slot in board cells, set by how large the UI draws the board.
    float visibilitySlotCells = 0.0f;
};

enum class SessionEventKind { BackendSwitched, BackendFailed };

struct SessionEvent {
    SessionEventKind kind = SessionEventKind::BackendSwitched;
    BackendKind backend = BackendKind::Scalar;
};

// Runs a GameSession on its own thread so that nothing the simulation does, from a slow physics step to building a
// backend, can hold up a UI frame. The UI posts commands, which run at the start of the next physics frame, and
// draws the latest SessionView. The physics thread runs at most one frame per UI frame, so when it keeps up the two
// work in lockstep, one frame apart and overlapping; when it cannot, the UI keeps drawing the latest view.
class SessionRunner {
public:
    using Command = std::function<void(GameSession&)>;

    SessionRunner(GameMode mode, LevelData level, BackendKind backend);
    // Does not wait for the physics thread: it finishes its frame, destroys the session and exits on its own.
    ~SessionRunner();
    SessionRunner(const SessionRunner&) = delete;
    SessionRunner& operator=(const SessionRunner&) = delete;

    void post(Command command);
    // Called once per UI frame; lets the physics thread run its next frame.
    void tick(const PhysicsFrameInput& input);
    // Null until the physics thread has built the session.
    std::shared_ptr<const SessionView> view() const;
    std::vector<SessionEvent> takeEvents();

    // Waits, up to the timeout, for the physics threads of destroyed runners to finish; used when the game quits.
    static bool waitForStoppedRunners(std::chrono::milliseconds timeout);

    // The state the runner and its physics thread share; the thread keeps it alive after the runner is gone.
    struct Shared;

private:
    std::shared_ptr<Shared> shared;
};
