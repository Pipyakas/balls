#include "game/SessionHost.h"

#include "level/LevelParser.h"

#include <utility>

SessionHost::SessionHost(GameMode mode, LevelData level, BackendKind backend, SessionThreading threading) {
    if (threading == SessionThreading::PhysicsThread) {
        runner = std::make_unique<SessionRunner>(mode, std::move(level), backend);
    } else {
        session = std::make_unique<GameSession>(mode, std::move(level), backend);
        refreshView();
    }
}

SessionHost::SessionHost(GameMode mode, LevelFiles files, BackendKind backend, SessionThreading threading,
                         std::shared_ptr<LoadProgress> progress) {
    if (threading == SessionThreading::PhysicsThread) {
        runner = std::make_unique<SessionRunner>(mode, std::move(files), backend, defaultTicksPerSecond,
                                                 std::move(progress));
        return;
    }
    auto level = loadLevel(files, progress.get());
    if (!level) {
        loadOutcome = LevelLoadOutcome{false, level.error()};
        return;
    }
    loadOutcome = LevelLoadOutcome{true, levelRepairNotice(*level)};
    session = std::make_unique<GameSession>(mode, std::move(*level), backend, defaultTicksPerSecond, progress.get());
    refreshView();
    if (progress) {
        progress->finish();
    }
}

std::optional<LevelLoadOutcome> SessionHost::takeLoadOutcome() {
    if (runner) {
        return runner->takeLoadOutcome();
    }
    return std::exchange(loadOutcome, std::nullopt);
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
        const PhysicsRate::Clock::time_point workStart = PhysicsRate::Clock::now();
        session->reportFrameWorkSeconds(input.uiWorkSeconds);
        session->update(deltaSeconds);
        rate.frame(workStart, PhysicsRate::Clock::now());
    }
    refreshView();
}

std::vector<SessionEvent> SessionHost::takeEvents() { return std::exchange(events, {}); }

void SessionHost::refreshView() {
    auto view = std::make_shared<SessionView>();
    describeSession(*session, *view, latest.get(), &boards);
    view->physicsFrameMs = rate.lastFrameMs();
    view->physicsFramesPerSecond = rate.framesPerSecond();
    latest = std::move(view);
}
