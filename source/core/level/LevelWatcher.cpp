#include "level/LevelWatcher.h"

#include <algorithm>

namespace {

constexpr auto pollInterval = std::chrono::milliseconds(250);
constexpr auto settleDelay = std::chrono::milliseconds(150);

}

void LevelWatcher::watch(const LevelFiles& files) {
    watchedFiles = files;
    lastStamp = readStamp();
    hasPending = false;
    state = LevelDiskState::Unchanged;
}

std::string_view levelDiskNotice(LevelDiskState state) {
    switch (state) {
    case LevelDiskState::Changed:
        return "Map changed on disk - Restart to load it";
    case LevelDiskState::Removed:
        return "Map removed from disk - Restart unavailable";
    case LevelDiskState::Unchanged:
        break;
    }
    return {};
}

LevelWatcher::FileTime LevelWatcher::readStamp() const {
    std::error_code bricksError;
    std::error_code launchersError;
    const FileTime bricksStamp = std::filesystem::last_write_time(watchedFiles.bricksPath, bricksError);
    const FileTime launchersStamp = std::filesystem::last_write_time(watchedFiles.launchersPath, launchersError);
    // Either file going missing is a change, even when the other one is newer.
    if (bricksError || launchersError) {
        return FileTime::min();
    }
    return std::max(bricksStamp, launchersStamp);
}

bool LevelWatcher::pollChanged(std::chrono::steady_clock::time_point now) {
    if (watchedFiles.bricksPath.empty() || now < nextPoll) {
        return false;
    }
    nextPoll = now + pollInterval;
    const FileTime stamp = readStamp();
    if (stamp != lastStamp && (!hasPending || stamp != pendingStamp)) {
        pendingStamp = stamp;
        pendingSince = now;
        hasPending = true;
        return false;
    }
    if (hasPending && now - pendingSince >= settleDelay) {
        hasPending = false;
        lastStamp = pendingStamp;
        std::error_code error;
        const bool present = std::filesystem::exists(watchedFiles.bricksPath, error) &&
                             std::filesystem::exists(watchedFiles.launchersPath, error);
        state = present ? LevelDiskState::Changed : LevelDiskState::Removed;
        return true;
    }
    return false;
}
