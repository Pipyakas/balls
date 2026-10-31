#pragma once

#include "level/LevelData.h"

#include "common/LoadProgress.h"
#include "common/Result.h"
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// The parsers repair what they can of a file that strays from the format and, given a list, add a line per repair.
using MapRepairs = std::vector<std::string>;

// Given a LoadProgress, the parse and the load report their steps to it as they go, for the loading screen.
Result<BrickLayout> parseBrickLayout(std::string_view text, MapRepairs* repairs = nullptr, LoadProgress* progress = nullptr);
Result<LauncherLayout> parseLauncherLayout(std::string_view text, MapRepairs* repairs = nullptr);
Result<LevelData> loadLevel(const LevelFiles& files, LoadProgress* progress = nullptr);
// The toast a repaired level shows when it starts, or empty when nothing was repaired.
std::string levelRepairNotice(const LevelData& level);
Result<std::string> readTextFile(const std::filesystem::path& path);
