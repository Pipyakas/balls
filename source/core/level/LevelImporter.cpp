#include "level/LevelImporter.h"

#include "level/LevelData.h"
#include "level/LevelParser.h"
#include "level/LevelRepository.h"

#include <algorithm>
#include <format>
#include <map>
#include <system_error>

namespace {

struct PickedPair {
    std::string name;
    std::filesystem::path sourceDirectory;
};

struct PairSearch {
    std::map<std::string, std::filesystem::path> pairs;
    std::vector<LevelImportResult> rejected;
};

std::string fileNameOf(const std::filesystem::path& path) { return path.filename().string(); }

LevelImportResult failure(std::string name, std::string message) {
    return {LevelImportResult::Status::Failed, std::move(name), "Import failed: " + std::move(message), false};
}

std::optional<std::string> levelNameOf(const std::string& fileName) {
    for (const std::string_view suffix : {bricksFileSuffix, launchersFileSuffix}) {
        if (fileName.size() > suffix.size() && fileName.ends_with(suffix)) {
            return fileName.substr(0, fileName.size() - suffix.size());
        }
    }
    return std::nullopt;
}

void collectFile(const std::filesystem::path& file, PairSearch& search) {
    const std::string fileName = fileNameOf(file);
    const auto name = levelNameOf(fileName);
    if (!name) {
        search.rejected.push_back(
            failure(fileName, std::format("{}: not a level file (expected <Name>{} or <Name>{})", fileName,
                                          bricksFileSuffix, launchersFileSuffix)));
        return;
    }
    search.pairs.try_emplace(*name, file.parent_path());
}

void collectFolder(const std::filesystem::path& folder, PairSearch& search) {
    const size_t pairsBefore = search.pairs.size();
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(folder, error)) {
        if (entry.is_regular_file() && levelNameOf(fileNameOf(entry.path()))) {
            collectFile(entry.path(), search);
        }
    }
    if (search.pairs.size() == pairsBefore) {
        search.rejected.push_back(failure(fileNameOf(folder), std::format("{}: no level files", fileNameOf(folder))));
    }
}

// A dropped file and its partner, or both files of a pair, name the same level: group by name so each pair is
// imported once.
PairSearch findPickedPairs(std::span<const std::filesystem::path> picked) {
    PairSearch search;
    for (const std::filesystem::path& path : picked) {
        std::error_code error;
        if (std::filesystem::is_directory(path, error)) {
            collectFolder(path, search);
        } else {
            collectFile(path, search);
        }
    }
    return search;
}

std::string mismatchWarning(const LevelData& level) {
    const ColorTally hits = countBrickHitsByColor(level.bricks);
    const ColorTally balls = countBallsByColor(level.launchers);
    std::string warning;
    for (const BallColor color : allBallColors) {
        const int colorHits = hits[toIndex(color)];
        const int colorBalls = balls[toIndex(color)];
        if (colorHits > 0 && colorBalls < colorHits) {
            warning += std::format("{}{} balls {} / hits {}", warning.empty() ? "" : ", ", ballColorName(color),
                                   colorBalls, colorHits);
        }
    }
    return warning;
}

std::optional<std::string> parseError(const std::filesystem::path& file, bool bricks) {
    const auto text = readTextFile(file);
    if (!text) {
        return std::format("{}: {}", fileNameOf(file), text.error());
    }
    if (bricks) {
        if (auto layout = parseBrickLayout(*text); !layout) {
            return std::format("{}: {}", fileNameOf(file), layout.error());
        }
    } else if (auto layout = parseLauncherLayout(*text); !layout) {
        return std::format("{}: {}", fileNameOf(file), layout.error());
    }
    return std::nullopt;
}

// Copies the spawn file first: the level list only shows a name once its bricks file exists, so it never sees
// a half-copied pair.
std::optional<std::string> copyPair(const LevelFiles& source, const LevelFiles& target) {
    for (const auto& [from, to] : {std::pair{source.launchersPath, target.launchersPath},
                                   std::pair{source.bricksPath, target.bricksPath}}) {
        std::error_code error;
        if (std::filesystem::exists(to, error) && std::filesystem::equivalent(from, to, error)) {
            continue;
        }
        std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, error);
        if (error) {
            return std::format("{}: {}", fileNameOf(to), error.message());
        }
    }
    return std::nullopt;
}

LevelImportResult importPair(const PickedPair& pair, const std::filesystem::path& destination, GameMode mode) {
    const LevelFiles source = levelFilesFor(pair.sourceDirectory, pair.name);
    for (const std::filesystem::path& file : {source.bricksPath, source.launchersPath}) {
        if (!std::filesystem::exists(file)) {
            return failure(pair.name, std::format("{} is missing", fileNameOf(file)));
        }
    }
    if (const auto error = parseError(source.bricksPath, true)) {
        return failure(pair.name, *error);
    }
    if (const auto error = parseError(source.launchersPath, false)) {
        return failure(pair.name, *error);
    }
    const auto level = loadLevel(source);
    if (!level) {
        return failure(pair.name, level.error());
    }

    std::error_code error;
    std::filesystem::create_directories(destination, error);
    const LevelFiles target = levelFilesFor(destination, pair.name);
    const bool replacing = std::filesystem::exists(target.bricksPath) || std::filesystem::exists(target.launchersPath);
    if (const auto copyError = copyPair(source, target)) {
        return failure(pair.name, *copyError);
    }

    LevelImportResult result{replacing ? LevelImportResult::Status::Replaced : LevelImportResult::Status::Imported,
                             pair.name, std::format("{} {}", replacing ? "Replaced" : "Imported", pair.name), false};
    if (mode == GameMode::Normal) {
        if (const std::string warning = mismatchWarning(*level); !warning.empty()) {
            result.message += " - warning: " + warning;
            result.hasWarning = true;
        }
    }
    return result;
}

}

size_t LevelImportReport::importedCount() const {
    return static_cast<size_t>(std::ranges::count_if(
        results, [](const LevelImportResult& result) { return result.status != LevelImportResult::Status::Failed; }));
}

bool LevelImportReport::anyFailed() const { return importedCount() != results.size(); }

std::string LevelImportReport::summary() const {
    if (results.empty()) {
        return "Import failed: no files";
    }
    if (results.size() == 1) {
        return results.front().message;
    }
    std::string text = std::format("Imported {} levels", importedCount());
    const auto failed = std::ranges::find(results, LevelImportResult::Status::Failed, &LevelImportResult::status);
    if (failed != results.end()) {
        text += std::format(" - {} failed, first: {}", results.size() - importedCount(), failed->message);
    }
    return text;
}

std::optional<std::string> LevelImportReport::levelToHighlight() const {
    for (auto result = results.rbegin(); result != results.rend(); ++result) {
        if (result->status != LevelImportResult::Status::Failed) {
            return result->name;
        }
    }
    return std::nullopt;
}

LevelImportReport importLevels(std::span<const std::filesystem::path> picked, const std::filesystem::path& destination,
                               GameMode mode) {
    PairSearch search = findPickedPairs(picked);
    LevelImportReport report;
    report.results = std::move(search.rejected);
    for (const auto& [name, directory] : search.pairs) {
        report.results.push_back(importPair({name, directory}, destination, mode));
    }
    return report;
}
