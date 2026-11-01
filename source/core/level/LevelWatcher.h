#pragma once

#include "level/LevelData.h"

#include <chrono>
#include <filesystem>
#include <string_view>

enum class LevelDiskState { Unchanged, Changed, Removed };

std::string_view levelDiskNotice(LevelDiskState state);

class LevelWatcher {
public:
    void watch(const LevelFiles& files);

    bool pollChanged(std::chrono::steady_clock::time_point now);

    LevelDiskState diskState() const { return state; }

    void markLoaded() { state = LevelDiskState::Unchanged; }

private:
    using FileTime = std::filesystem::file_time_type;

    FileTime readStamp() const;

    LevelFiles watchedFiles;
    FileTime lastStamp{};
    FileTime pendingStamp{};
    std::chrono::steady_clock::time_point nextPoll{};
    std::chrono::steady_clock::time_point pendingSince{};
    bool hasPending = false;
    LevelDiskState state = LevelDiskState::Unchanged;
};
