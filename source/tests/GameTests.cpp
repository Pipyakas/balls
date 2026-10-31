#include "TestFramework.h"

#include "game/GameSession.h"
#include "level/LevelParser.h"
#include "level/LevelRepository.h"

#include <cmath>
#include <algorithm>
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

TEST_CASE("performance mode keeps releasing launchers once the board is cleared") {
    LevelData level = loadSample("performance", "DA_1000Bricks");
    std::fill(level.bricks.cellIds.begin(), level.bricks.cellIds.end(), emptyBrickId);
    GameSession session(GameMode::Performance, std::move(level), BackendKind::Scalar);
    const int launchers = session.launchers().launcherCount();
    session.selectSpawnRate(static_cast<int>(spawnRateOptions.size()) - 1);
    session.startAutoRelease();
    for (int frame = 0; frame < 60; ++frame) {
        session.update(1.0f / 60.0f);
    }
    EXPECT(session.state() == SessionState::Running);
    EXPECT(session.launchers().launcherCount() < launchers - 1);
}

TEST_CASE("performance auto release spawns every color at once, however big each launcher is") {
    LevelData level = loadSample("performance", "Unlimited_921Bricks");
    std::fill(level.bricks.cellIds.begin(), level.bricks.cellIds.end(), emptyBrickId);
    GameSession session(GameMode::Performance, std::move(level), BackendKind::Scalar);
    session.startAutoRelease();
    for (int frame = 0; frame < 60; ++frame) {
        session.update(1.0f / 60.0f);
    }
    for (const BallCount balls : session.simulation().stats().ballsByColor) {
        EXPECT(balls > 0);
    }
}

TEST_CASE("Auto is the backend's capacity, and the ramp fills toward it over frames") {
    GameSession session(GameMode::Performance, loadSample("performance", "DA_1000Bricks"), BackendKind::Scalar);
    session.useCapacityBallLimit();
    EXPECT(session.isUsingMachineBallLimit());
    EXPECT(session.ballLimit() == static_cast<BallCount>(session.simulation().backend().ballCapacityLimit()));
    EXPECT(session.simulation().liveBallCount() == 0);
    session.fillToBallLimit();
    for (int frame = 0; frame < 10; ++frame) {
        session.update(1.0f / 60.0f);
    }
    EXPECT(session.simulation().liveBallCount() > 0);
    session.selectBallLimit(0);
    EXPECT(session.ballLimit() == ballLimitOptions[0]);
}

TEST_CASE("performance session waits for one click then releases automatically") {
    GameSession session(GameMode::Performance, loadSample("performance", "DA_1000Bricks"), BackendKind::Scalar);
    session.selectSpawnRate(static_cast<int>(spawnRateOptions.size()) - 1);
    session.update(1.0f / 60.0f);
    EXPECT(session.simulation().stats().totalBallsSpawned == 0);
    session.selectSpawnRate(static_cast<int>(spawnRateOptions.size()) - 1);
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

TEST_CASE("a performance game starts at the fastest spawn rate and the largest ball count") {
    GameSession session(GameMode::Performance, loadSample("performance", "DA_1000Bricks"), BackendKind::Scalar);
    EXPECT(spawnRateOptions[session.spawnRateIndex()] == 0);
    EXPECT(ballMultiplierOptions[session.ballMultiplierIndex()] == ballMultiplierOptions.back());
    session.selectSpawnRate(2);
    session.restart();
    EXPECT(session.spawnRateIndex() == 2);
}

namespace {

BallCount launcherBallsLeft(const GameSession& session) {
    BallCount total = 0;
    for (const int balls : session.launchers().remainingBalls()) {
        total += static_cast<BallCount>(balls);
    }
    return total;
}

// Balls the first auto-release frame took from the launchers, and the balls it handed on (spawned or still waiting).
std::pair<BallCount, BallCount> firstReleaseFrame(bool overrideCounts) {
    GameSession session(GameMode::Performance, loadSample("performance", "DA_1000Bricks"), BackendKind::Scalar);
    session.setLauncherCountOverride(overrideCounts);
    session.selectSpawnRate(0);
    const BallCount before = launcherBallsLeft(session);
    session.startAutoRelease();
    session.update(0.0f);
    return {before - launcherBallsLeft(session),
            session.spawner().pendingBalls() + session.simulation().liveBallCount()};
}

}

TEST_CASE("performance launchers release the level's counts unless Override launcher counts is on") {
    const auto [taken, released] = firstReleaseFrame(false);
    EXPECT(taken > 0);
    EXPECT(released == taken);
    const auto [takenOverridden, releasedOverridden] = firstReleaseFrame(true);
    EXPECT(releasedOverridden == takenOverridden * ballMultiplierOptions.back());
}
