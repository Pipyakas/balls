#include "game/SessionHost.h"

#include <utility>

namespace {

// The window SessionRunner averages its physics frame rate over.
constexpr double rateWindowSeconds = 0.5;

} // namespace

SessionHost::SessionHost(GameMode mode, LevelData level, BackendKind backend, SessionThreading threading) {
    if (threading == SessionThreading::PhysicsThread) {
        runner = std::make_unique<SessionRunner>(mode, std::move(level), backend);
    } else {
        session = std::make_unique<GameSession>(mode, std::move(level), backend);
        refreshView();
    }
}

void SessionHost::post(SessionRunner::Command command) {
    if (runner) {
        runner->post(std::move(command));
        return;
    }
    command(*session);
    refreshView();
}

void SessionHost::setBallCollisions(bool enabled) {
    if (runner) {
        runner->setBallCollisions(enabled);
        return;
    }
    const BallCollisionsChange change = session->setBallCollisions(enabled);
    if (change != BallCollisionsChange::None) {
        SessionEvent event{SessionEventKind::BallCollisionsChanged, session->backend()};
        event.collisionsChange = change;
        events.push_back(event);
    }
    refreshView();
}

void SessionHost::tick(const PhysicsFrameInput& input, float deltaSeconds) {
    if (runner) {
        runner->tick(input);
        if (auto newest = runner->view()) {
            latest = std::move(newest);
        }
        for (SessionEvent& event : runner->takeEvents()) {
            events.push_back(event);
        }
        return;
    }
    // SessionRunner's order: adopt a backend built off the frame before the tick that would run on it.
    session->pollBackgroundWork();
    if (const auto outcome = session->takeBackendSwitchOutcome()) {
        events.push_back({outcome->succeeded ? SessionEventKind::BackendSwitched : SessionEventKind::BackendFailed,
                          outcome->kind, outcome->forBallCollisions});
    }
    if (!input.paused) {
        using Clock = std::chrono::steady_clock;
        const Clock::time_point workStart = Clock::now();
        session->reportFrameSeconds(input.uiFrameSeconds, input.uiWorkSeconds);
        session->update(deltaSeconds);
        const Clock::time_point workEnd = Clock::now();
        lastFrameMs = std::chrono::duration<double, std::milli>(workEnd - workStart).count();
        ++framesInWindow;
        if (const double windowSeconds = std::chrono::duration<double>(workEnd - rateWindowStart).count();
            windowSeconds >= rateWindowSeconds) {
            framesPerSecond = static_cast<double>(framesInWindow) / windowSeconds;
            framesInWindow = 0;
            rateWindowStart = workEnd;
        }
    }
    refreshView();
}

std::vector<SessionEvent> SessionHost::takeEvents() { return std::exchange(events, {}); }

void SessionHost::refreshView() {
    auto view = std::make_shared<SessionView>();
    describeSession(*session, *view, latest.get());
    view->physicsFrameMs = lastFrameMs;
    view->physicsFramesPerSecond = framesPerSecond;
    latest = std::move(view);
}
