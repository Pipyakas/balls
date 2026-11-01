#include "TestFramework.h"

#include "game/SessionHost.h"
#include "game/SessionRunner.h"
#include "level/LevelParser.h"
#include "level/LevelRepository.h"
#include "ui/LoadingScreen.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <random>
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

std::shared_ptr<const SessionView> tickUntil(SessionRunner& runner,
                                             const std::function<bool(const SessionView&)>& condition,
                                             bool paused = false) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline) {
        runner.tick({paused, 0.001, 0.0f});
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

TEST_CASE("a session host on the UI thread steps the game inside its tick") {
    SessionHost host(GameMode::Normal, loadSample("normal", "DA_FirstLevel"), BackendKind::Scalar, SessionThreading::UiThread);
    EXPECT(!host.isThreaded());
    EXPECT(host.view() != nullptr);
    host.post([](GameSession& session) { session.chooseLauncher(0, 0); });
    EXPECT(host.view()->pendingSpawn > 0);
    for (int frame = 0; frame < 30; ++frame) {
        host.tick({false, 0.001, 0.0f}, 1.0f / 60.0f);
    }
    EXPECT(host.view()->stats.totalBallsSpawned > 0);
    const double elapsed = host.view()->elapsedSeconds;
    host.tick({true, 0.001, 0.0f}, 1.0f / 60.0f);
    EXPECT(host.view()->elapsedSeconds == elapsed);
}

TEST_CASE("a session host on the physics thread reads the runner's views") {
    SessionHost host(GameMode::Normal, loadSample("normal", "DA_FirstLevel"), BackendKind::Scalar,
                     SessionThreading::PhysicsThread);
    EXPECT(host.isThreaded());
    EXPECT(host.liveSession() == nullptr);
    host.post([](GameSession& session) { session.chooseLauncher(0, 0); });
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline &&
           !(host.view() && host.view()->stats.totalBallsSpawned > 0)) {
        host.tick({false, 0.001, 0.0f}, 0.005f);
        std::this_thread::sleep_for(5ms);
    }
    EXPECT(host.view() && host.view()->stats.totalBallsSpawned > 0);
}

TEST_CASE("a runner given level files reads them on its thread and reports each loading step") {
    auto progress = std::make_shared<LoadProgress>();
    SessionRunner runner(GameMode::Normal,
                         levelFilesFor(std::filesystem::path(BALLS_ASSET_DIR) / "levels" / "normal", "DA_FirstLevel"),
                         BackendKind::Scalar, defaultTicksPerSecond, progress);
    const auto view = tickUntil(runner, [](const SessionView&) { return true; });
    EXPECT(view != nullptr);
    const auto outcome = runner.takeLoadOutcome();
    EXPECT(outcome && outcome->loaded && outcome->message.empty());
    EXPECT(!runner.takeLoadOutcome());
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!progress->isFinished() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(5ms);
    }
    EXPECT(progress->isFinished());
    std::vector<std::string> texts;
    for (const LoadProgress::Step& step : progress->steps()) {
        EXPECT(step.done);
        texts.push_back(step.text);
    }
    EXPECT(texts.size() >= 6);
    EXPECT(texts.front() == "Reading DA_FirstLevel_Bricks.txt");
    EXPECT(std::ranges::find(texts, "Starting Scalar physics") != texts.end());
    EXPECT(texts.back() == "Drawing the first frame");
}

TEST_CASE("a runner whose level files do not parse reports why and builds no session") {
    const std::filesystem::path folder =
        std::filesystem::temp_directory_path() / ("balls-runner-load-" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(folder);
    std::ofstream(folder / "Bad_Bricks.txt") << "[Board]\nWidth 1\nHeight 1\n[Layout]\nabc\n";
    std::ofstream(folder / "Bad_SpawnBall.txt") << "[Layout]\nRed(1)\n";
    auto progress = std::make_shared<LoadProgress>();
    SessionRunner runner(GameMode::Normal, levelFilesFor(folder, "Bad"), BackendKind::Scalar, defaultTicksPerSecond,
                         progress);
    std::optional<LevelLoadOutcome> outcome;
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!(outcome = runner.takeLoadOutcome()) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(5ms);
    }
    EXPECT(outcome && !outcome->loaded && outcome->message.starts_with("Bad_Bricks.txt: "));
    EXPECT(progress->isFinished());
    EXPECT(runner.view() == nullptr);
    std::error_code error;
    std::filesystem::remove_all(folder, error);
}

TEST_CASE("the loading screen lists each step with its time, the running one last with its detail") {
    LoadProgress progress;
    EXPECT(loadingScreenText("Arena", nullptr).lines.empty());
    progress.begin("Reading Arena_Bricks.txt");
    progress.setDetail("1.5 MB");
    progress.begin("Reading the 4 x 2 brick layout");
    progress.setDetail("row 64 of 128");
    const LoadingScreenText running = loadingScreenText("Arena", &progress);
    EXPECT(running.title == "Loading Arena");
    EXPECT(running.lines.size() == 2);
    EXPECT(running.lines[0].text == "Reading Arena_Bricks.txt: 1.5 MB" && !running.lines[0].running);
    EXPECT(running.lines[1].text.starts_with("Reading the 4 x 2 brick layout: row 64 of 128") &&
           running.lines[1].running);
    EXPECT(running.lines[1].time.ends_with(" s") && running.total.ends_with(" s total"));
    for (size_t step = 0; step < loadingScreenMaxLines; ++step) {
        progress.begin(std::format("Step {}", step));
    }
    progress.finish();
    const LoadingScreenText finished = loadingScreenText("Arena", &progress);
    EXPECT(finished.lines.size() == loadingScreenMaxLines);
    EXPECT(finished.lines.front().text == "Step 0" && !finished.lines.back().running);
}
