#pragma once

#include "game/PhysicsRate.h"
#include "game/SessionRunner.h"
#include "game/SessionView.h"

#include <memory>
#include <optional>
#include <vector>

enum class SessionThreading { UiThread, PhysicsThread };

class SessionHost {
public:
    SessionHost(GameMode mode, LevelData level, BackendKind backend, SessionThreading threading);

    SessionHost(GameMode mode, LevelFiles files, BackendKind backend, SessionThreading threading,
                std::shared_ptr<LoadProgress> progress);
    SessionHost(const SessionHost&) = delete;
    SessionHost& operator=(const SessionHost&) = delete;

    bool isThreaded() const { return runner != nullptr; }

    void post(SessionRunner::Command command);

    void setBallCollisions(bool enabled);

    void tick(const PhysicsFrameInput& input, float deltaSeconds);

    std::shared_ptr<const SessionView> view() const { return latest; }

    std::vector<SessionEvent> takeEvents();

    std::optional<LevelLoadOutcome> takeLoadOutcome();

    GameSession* liveSession() { return session.get(); }

private:
    void refreshView();

    std::unique_ptr<GameSession> session;
    std::unique_ptr<SessionRunner> runner;
    std::shared_ptr<const SessionView> latest;
    BoardSnapshots boards;
    std::vector<SessionEvent> events;
    std::optional<LevelLoadOutcome> loadOutcome;

    PhysicsRate rate;
};
