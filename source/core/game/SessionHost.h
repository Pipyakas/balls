#pragma once

#include "game/PhysicsRate.h"
#include "game/SessionRunner.h"
#include "game/SessionView.h"

#include <memory>
#include <vector>

// Where a session's physics runs: inside the UI's frame, or on a SessionRunner's own thread, where a long physics step
// overlaps the UI's frame instead of adding to it.
enum class SessionThreading { UiThread, PhysicsThread };

// One game session that a UI drives the same way on either thread: it posts commands, ticks once per frame and reads
// the latest SessionView. SDL uses SessionRunner directly; this lets a front end keep both, to compare them or fall
// back. On the UI thread the view is rebuilt after each tick, without the balls: the UI draws them straight from the
// backend through liveSession().
class SessionHost {
public:
    SessionHost(GameMode mode, LevelData level, BackendKind backend, SessionThreading threading);
    SessionHost(const SessionHost&) = delete;
    SessionHost& operator=(const SessionHost&) = delete;

    bool isThreaded() const { return runner != nullptr; }
    // On the UI thread the command runs at once; on the physics thread at the start of its next frame.
    void post(SessionRunner::Command command);
    // GameSession::setBallCollisions; anything other than None comes back as a BallCollisionsChanged event.
    void setBallCollisions(bool enabled);
    // Once per UI frame. On the UI thread it steps the session by deltaSeconds unless paused, using the input's frame
    // times for the addition governor; on the physics thread it hands the input over and takes the newest view.
    void tick(const PhysicsFrameInput& input, float deltaSeconds);
    // Null only while the physics thread has not published its first view.
    std::shared_ptr<const SessionView> view() const { return latest; }
    // Backend switches and ball collisions changes since the last call, in order.
    std::vector<SessionEvent> takeEvents();
    // The session itself while it runs on the UI thread, for drawing straight from its backend; null otherwise.
    GameSession* liveSession() { return session.get(); }

private:
    void refreshView();

    std::unique_ptr<GameSession> session;
    std::unique_ptr<SessionRunner> runner;
    std::shared_ptr<const SessionView> latest;
    BoardSnapshots boards;
    std::vector<SessionEvent> events;
    // On the game thread a physics frame is a tick that stepped the session.
    PhysicsRate rate;
};
