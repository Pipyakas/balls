#pragma once

#include "game/GameMode.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

// One level pair taken from the files a player picked or dropped.
struct LevelImportResult {
    enum class Status { Imported, Replaced, Failed };

    Status status = Status::Failed;
    std::string name;
    std::string message;
    bool hasWarning = false;
};

struct LevelImportReport {
    std::vector<LevelImportResult> results;

    size_t importedCount() const;
    bool anyFailed() const;
    // The toast text: the single result's message, or one summary line when several pairs were imported.
    std::string summary() const;
    // The level the list should scroll to and highlight: the last pair that made it into the folder.
    std::optional<std::string> levelToHighlight() const;
};

// Copies level pairs into a mode's level folder. Each entry of `picked` may be either file of a pair or a
// folder of pairs; the partner is looked up next to the picked file by name. Every pair is parsed before
// anything is copied, so a broken file never lands in the folder. Normal-mode ball/hit mismatches are
// reported as warnings but still imported, because the sample maps are loaded as given.
LevelImportReport importLevels(std::span<const std::filesystem::path> picked, const std::filesystem::path& destination,
                               GameMode mode);
