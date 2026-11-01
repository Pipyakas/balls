#pragma once

#include "game/GameMode.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

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

    std::string summary() const;

    std::optional<std::string> levelToHighlight() const;
};

LevelImportReport importLevels(std::span<const std::filesystem::path> picked, const std::filesystem::path& destination,
                               GameMode mode);
