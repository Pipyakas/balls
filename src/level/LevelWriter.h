#pragma once

#include "level/LevelData.h"

#include <expected>
#include <string>

std::string formatBrickLayout(const BrickLayout& layout);
std::string formatLauncherLayout(const LauncherLayout& layout);
std::expected<void, std::string> saveLevel(const LevelData& level, const LevelFiles& files);
