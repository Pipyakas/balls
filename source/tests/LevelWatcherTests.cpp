#include "TestFramework.h"

#include "level/LevelRepository.h"
#include "level/LevelWatcher.h"

#include <filesystem>
#include <fstream>

namespace {

using namespace std::chrono_literals;

bool settle(LevelWatcher& watcher, std::chrono::steady_clock::time_point& now) {
    bool changed = false;
    for (int step = 0; step < 4; ++step) {
        const std::chrono::steady_clock::time_point pollTime = now + 300ms;
        now = pollTime;
        changed |= watcher.pollChanged(pollTime);
    }
    return changed;
}

}

TEST_CASE("the level watcher latches a change until the level is loaded again") {
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "balls_watcher_test";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    const LevelFiles files = levelFilesFor(folder, "Watched");
    std::ofstream(files.bricksPath) << "1";
    std::ofstream(files.launchersPath) << "1";

    LevelWatcher watcher;
    watcher.watch(files);
    auto now = std::chrono::steady_clock::now();
    EXPECT(!settle(watcher, now));
    EXPECT(watcher.diskState() == LevelDiskState::Unchanged);
    EXPECT(levelDiskNotice(watcher.diskState()).empty());

    const auto before = std::filesystem::last_write_time(files.bricksPath);
    std::filesystem::last_write_time(files.bricksPath, before + 5s);
    EXPECT(std::filesystem::last_write_time(files.bricksPath) != before);
    EXPECT(settle(watcher, now));
    EXPECT(watcher.diskState() == LevelDiskState::Changed);
    EXPECT(levelDiskNotice(watcher.diskState()) == "Map changed on disk - Restart to load it");
    watcher.markLoaded();
    EXPECT(watcher.diskState() == LevelDiskState::Unchanged);

    std::filesystem::remove(files.launchersPath);
    EXPECT(settle(watcher, now));
    EXPECT(watcher.diskState() == LevelDiskState::Removed);
    std::filesystem::remove_all(folder);
}
