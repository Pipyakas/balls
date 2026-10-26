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
}

LevelWatcher::FileTime LevelWatcher::readStamp() const {
    std::error_code error;
    const FileTime bricksStamp = std::filesystem::last_write_time(watchedFiles.bricksPath, error);
    const FileTime launchersStamp = std::filesystem::last_write_time(watchedFiles.launchersPath, error);
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
        return true;
    }
    return false;
}
