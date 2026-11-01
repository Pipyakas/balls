#pragma once

#include "game/GameMode.h"

#include <filesystem>
#include <string_view>

std::filesystem::path assetRoot();
std::filesystem::path levelDirectory(GameMode mode);
std::filesystem::path fontPath(std::string_view fileName);
std::filesystem::path pathFromUtf8(const char* text);
std::string utf8Of(const std::filesystem::path& path);
