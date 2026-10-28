#include "TestFramework.h"

#include "game/GameSession.h"
#include "level/LevelParser.h"
#include "level/LevelRepository.h"

#include <cmath>
#include <filesystem>

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

TEST_CASE("spawn pacer caps a frame's additions by what making them costs") {
    SpawnPacer pacer;
    constexpr double secondsPerBall = 100e-9;
    constexpr BallCount batch = 1'000'000;
    for (int frame = 0; frame < 60; ++frame) {
        pacer.recordSpawn(batch, secondsPerBall * static_cast<double>(batch));
    }
    const double expected = pacedFrameSeconds * spawnShareOfFrame / secondsPerBall;
    EXPECT(std::abs(static_cast<double>(pacer.allowance()) - expected) < expected * 0.01);
}

TEST_CASE("spawn pacer lets the balls on the board at most double per second") {
    SpawnPacer pacer;
    constexpr BallCount live = 1'000'000;
    constexpr double frame = 1.0 / 60.0;
    pacer.reportFrame(frame, live);
    const double expected = static_cast<double>(live) * (std::exp2(liveBallDoublingsPerSecond * frame) - 1.0);
    EXPECT(std::abs(static_cast<double>(pacer.allowance()) - expected) < expected * 0.01);
}

TEST_CASE("a large fill reaches the board over several frames, not in one") {
    GameSession session(GameMode::Performance, loadSample("performance", "DA_1000Bricks"), BackendKind::Scalar);
    constexpr BallCount fill = 200'000;
    session.fillWithBalls(fill);
    session.reportFrameSeconds(0.004, 0.004);
    session.update(1.0f / 60.0f);
    EXPECT(session.simulation().liveBallCount() < fill);
    for (int frame = 0; frame < 600 && session.queuedLaunches() > 0; ++frame) {
        session.reportFrameSeconds(0.004, 0.004);
        session.update(1.0f / 60.0f);
    }
    EXPECT(session.queuedLaunches() == 0);
    EXPECT(session.simulation().stats().totalBallsSpawned == fill);
}
