#include "TestFramework.h"

#include "game/GameSession.h"
#include "level/LevelParser.h"
#include "level/LevelRepository.h"

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
    while (session.chooseLauncher(0, 0)) {
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
