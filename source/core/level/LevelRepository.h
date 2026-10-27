#pragma once

#include "level/LevelData.h"

#include <filesystem>
#include <vector>

inline constexpr std::string_view bricksFileSuffix = "_Bricks.txt";
inline constexpr std::string_view launchersFileSuffix = "_SpawnBall.txt";

std::vector<LevelFiles> findLevels(const std::filesystem::path& directory);
LevelFiles levelFilesFor(const std::filesystem::path& directory, const std::string& name);
