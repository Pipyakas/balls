#pragma once

#include "level/LevelData.h"

#include "common/Result.h"
#include <filesystem>
#include <string>
#include <string_view>

Result<BrickLayout> parseBrickLayout(std::string_view text);
Result<LauncherLayout> parseLauncherLayout(std::string_view text);
Result<LevelData> loadLevel(const LevelFiles& files);
Result<std::string> readTextFile(const std::filesystem::path& path);
