#include "TestFramework.h"

#include "game/SessionRunner.h"
#include "level/LevelParser.h"
#include "level/LevelRepository.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <thread>

namespace {

using namespace std::chrono_literals;

LevelData loadSample(const char* folder, const char* name) {
    auto level = loadLevel(levelFilesFor(std::filesystem::path(BALLS_ASSET_DIR) / "levels" / folder, name));
    if (!level) {
        throw TestFailure{level.error()};
    }
    return *level;
}

// Ticks the runner like a UI at about 200 frames a second until the condition holds or two seconds pass.
std::shared_ptr<const SessionView> tickUntil(SessionRunner& runner,
                                             const std::function<bool(const SessionView&)>& condition,
                                             bool paused = false) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline) {
        runner.tick({paused, 0.005, 0.001, 0.0f});
        if (const auto view = runner.view(); view && condition(*view)) {
            return view;
        }
        std::this_thread::sleep_for(5ms);
    }
    return runner.view();
}

}

TEST_CASE("the physics thread publishes a view of the session it builds") {
    SessionRunner runner(GameMode::Normal, loadSample("normal", "DA_FirstLevel"), BackendKind::Scalar);
    const auto view = tickUntil(runner, [](const SessionView&) { return true; });
    EXPECT(view != nullptr);
    EXPECT(view->state == SessionState::Running);
    EXPECT(view->board->aliveBrickCount() > 0);
    EXPECT(view->launchers.launcherCount() == 7);
}

TEST_CASE("posted commands run on the session and show in a later view") {
    SessionRunner runner(GameMode::Normal, loadSample("normal", "DA_FirstLevel"), BackendKind::Scalar);
    tickUntil(runner, [](const SessionView&) { return true; });
    runner.post([](GameSession& session) { session.chooseLauncher(0, 0); });
    const auto view = tickUntil(runner, [](const SessionView& view) { return view.stats.totalBallsSpawned > 0; });
    EXPECT(view->state == SessionState::Running);
    EXPECT(view->launchers.launcherCount() == 6);
    EXPECT(view->stats.totalBallsSpawned > 0);
}

TEST_CASE("a paused runner keeps publishing views without advancing the game") {
    SessionRunner runner(GameMode::Normal, loadSample("normal", "DA_FirstLevel"), BackendKind::Scalar);
    tickUntil(runner, [](const SessionView&) { return true; });
    runner.post([](GameSession& session) { session.chooseLauncher(0, 0); });
    // A frame already under way when the pause was set read the input before it and still advances the game, so the
    // paused baseline is a frame that started after it: two past the latest one at the time.
    const uint64_t pausedAt = tickUntil(runner, [](const SessionView&) { return true; }, true)->frameNumber;
    const auto first = tickUntil(runner, [&](const SessionView& view) { return view.frameNumber > pausedAt + 1; }, true);
    EXPECT(first->state == SessionState::Running);
    const double elapsed = first->elapsedSeconds;
    const auto later = tickUntil(runner, [&](const SessionView& view) { return view.frameNumber > first->frameNumber + 5; },
                                 true);
    EXPECT(later->frameNumber > first->frameNumber);
    EXPECT(later->elapsedSeconds == elapsed);
}

TEST_CASE("destroyed runners stop their physics threads") {
    {
        SessionRunner runner(GameMode::Normal, loadSample("normal", "DA_FirstLevel"), BackendKind::Scalar);
        tickUntil(runner, [](const SessionView&) { return true; });
    }
    EXPECT(SessionRunner::waitForStoppedRunners(2s));
}
