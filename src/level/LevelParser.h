#pragma once

#include "level/LevelData.h"

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

std::expected<BrickLayout, std::string> parseBrickLayout(std::string_view text);
std::expected<LauncherLayout, std::string> parseLauncherLayout(std::string_view text);
std::expected<LevelData, std::string> loadLevel(const LevelFiles& files);
std::expected<std::string, std::string> readTextFile(const std::filesystem::path& path);
