#pragma once

#include "level/LevelData.h"

#include <chrono>
#include <filesystem>
#include <string_view>

// Whether the level in play still matches its files (a change on disk is not loaded until Restart).
enum class LevelDiskState { Unchanged, Changed, Removed };

// The header notice for the state; empty for Unchanged.
std::string_view levelDiskNotice(LevelDiskState state);

class LevelWatcher {
public:
    void watch(const LevelFiles& files);
    // True once per settled change of the files; it also updates diskState.
    bool pollChanged(std::chrono::steady_clock::time_point now);
    // Changed stays until markLoaded, so the notice outlives an edit that is undone; Removed follows the files.
    LevelDiskState diskState() const { return state; }
    // The game has just loaded the files as they are now.
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
