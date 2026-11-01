#pragma once

#include "level/LevelData.h"

#include "common/LoadProgress.h"
#include "common/Result.h"
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

using MapRepairs = std::vector<std::string>;

Result<BrickLayout> parseBrickLayout(std::string_view text, MapRepairs* repairs = nullptr, LoadProgress* progress = nullptr);
Result<LauncherLayout> parseLauncherLayout(std::string_view text, MapRepairs* repairs = nullptr);
Result<LevelData> loadLevel(const LevelFiles& files, LoadProgress* progress = nullptr);

std::string levelRepairNotice(const LevelData& level);
Result<std::string> readTextFile(const std::filesystem::path& path);
