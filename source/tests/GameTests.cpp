#include "TestFramework.h"

#include "game/GameSession.h"
#include "level/LevelParser.h"
#include "level/LevelRepository.h"

#include <chrono>
#include <filesystem>
#include <thread>

namespace {

LevelData loadSample(const char* folder, const char* name) {
    auto level = loadLevel(levelFilesFor(std::filesystem::path(BALLS_ASSET_DIR) / "levels" / folder, name));
    if (!level) {
        throw TestFailure{level.error()};
    }
    return *level;
}

}

TEST_CASE("choosing a launcher removes it and shifts its column up") {
    LauncherQueue queue;
    queue.load(loadSample("normal", "DA_49Bricks").launchers);
    const Launcher second = queue.visible(0, 1)->launcher;
    const int before = queue.launcherCount();
    EXPECT(queue.take(0, 0).has_value());
    EXPECT(queue.launcherCount() == before - 1);
    EXPECT(queue.visible(0, 0)->launcher.color == second.color);
    EXPECT(queue.visible(0, 0)->launcher.ballCount == second.ballCount);
}

TEST_CASE("normal session releases exactly the launcher's ball count") {
    GameSession session(GameMode::Normal, loadSample("normal", "DA_FirstLevel"), BackendKind::Scalar);
    const int count = session.launchers().visible(0, 0)->launcher.ballCount;
    EXPECT(session.chooseLauncher(0, 0));
    for (int frame = 0; frame < 120; ++frame) {
        session.update(1.0f / 60.0f);
    }
    EXPECT(session.simulation().stats().totalBallsSpawned == static_cast<uint64_t>(count));
}

TEST_CASE("normal level can be cleared by releasing every launcher") {
    GameSession session(GameMode::Normal, loadSample("normal", "DA_FirstLevel"), BackendKind::Scalar);
    for (int column = 0; column < session.launchers().columnCount(); ++column) {
        while (session.chooseLauncher(column, 0)) {
        }
    }
    for (int frame = 0; frame < 60 * 120 && session.state() != SessionState::Won; ++frame) {
        session.update(1.0f / 60.0f);
    }
    EXPECT(session.state() == SessionState::Won);
}

TEST_CASE("a capacity probe jump is launched within a frame budget and completes over frames") {
    GameSession session(GameMode::Performance, loadSample("performance", "DA_1000Bricks"), BackendKind::Scalar);
    session.startCapacityProbe(60.0);
    // The probe's first jump must be in the simulation after a few frames, and the probe must not have moved on to
    // measuring while part of it was still waiting to be launched.
    for (int frame = 0; frame < 600 && session.simulation().liveBallCount() < probeInitialBalls; ++frame) {
        EXPECT(session.capacityProbe().phase() != ProbePhase::Measuring);
        session.reportFrameSeconds(1.0 / 240.0, 1.0 / 240.0);
    }
    EXPECT(session.simulation().liveBallCount() >= probeInitialBalls);
}

TEST_CASE("performance session waits for one click then releases automatically") {
    GameSession session(GameMode::Performance, loadSample("performance", "DA_1000Bricks"), BackendKind::Scalar);
    session.selectSpawnRate(static_cast<int>(spawnRateOptions.size()) - 1);
    session.update(1.0f / 60.0f);
    EXPECT(session.simulation().stats().totalBallsSpawned == 0);
    session.startAutoRelease();
    for (int frame = 0; frame < 10; ++frame) {
        session.update(1.0f / 60.0f);
    }
    EXPECT(session.simulation().stats().totalBallsSpawned > 0);
}

TEST_CASE("ball collisions on Scalar take effect at once") {
    GameSession session(GameMode::Normal, loadSample("normal", "DA_FirstLevel"), BackendKind::Scalar);
    EXPECT(session.setBallCollisions(true) == BallCollisionsChange::None);
    EXPECT(session.simulation().ballCollisionsActive());
    EXPECT(!session.pendingBackend().has_value());
    EXPECT(session.setBallCollisions(false) == BallCollisionsChange::None);
    EXPECT(!session.simulation().ballCollisionsRequested());
}

TEST_CASE("ball collisions on another backend move to Scalar only where Scalar is offered") {
    if (!isBackendAvailable(BackendKind::Avx2)) {
        return;
    }
    GameSession session(GameMode::Normal, loadSample("normal", "DA_FirstLevel"), BackendKind::Avx2);
    const BallCollisionsChange change = session.setBallCollisions(true);
    EXPECT(session.simulation().ballCollisionsRequested());
    if (!isBackendSelectable(BackendKind::Scalar)) {
        EXPECT(change == BallCollisionsChange::ScalarNotOffered);
        EXPECT(!session.pendingBackend().has_value());
        return;
    }
    EXPECT(change == BallCollisionsChange::SwitchingToScalar);
    EXPECT(session.pendingBackend() == BackendKind::Scalar);
    std::optional<BackendSwitchOutcome> outcome;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!outcome && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        session.pollBackgroundWork();
        outcome = session.takeBackendSwitchOutcome();
    }
    EXPECT(outcome && outcome->succeeded && outcome->forBallCollisions);
    EXPECT(session.simulation().ballCollisionsActive());
}

TEST_CASE("every ball collisions change has the game's toast") {
    EXPECT(ballCollisionsMessage(BallCollisionsChange::None).empty());
    EXPECT(ballCollisionsMessage(BallCollisionsChange::SwitchingToScalar) ==
           "Ball collisions need the Scalar backend - switching to Scalar");
    EXPECT(!ballCollisionsMessage(BallCollisionsChange::ScalarNotOffered).empty());
}
