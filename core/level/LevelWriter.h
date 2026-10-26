#pragma once

#include "level/LevelData.h"

#include "common/Result.h"
#include <string>

std::string formatBrickLayout(const BrickLayout& layout);
std::string formatLauncherLayout(const LauncherLayout& layout);
Result<void> saveLevel(const LevelData& level, const LevelFiles& files);
