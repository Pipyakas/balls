#pragma once

#include "level/LevelData.h"

#include <chrono>
#include <filesystem>

class LevelWatcher {
public:
    void watch(const LevelFiles& files);
    bool pollChanged(std::chrono::steady_clock::time_point now);

private:
    using FileTime = std::filesystem::file_time_type;

    FileTime readStamp() const;

    LevelFiles watchedFiles;
    FileTime lastStamp{};
    FileTime pendingStamp{};
    std::chrono::steady_clock::time_point nextPoll{};
    std::chrono::steady_clock::time_point pendingSince{};
    bool hasPending = false;
};
