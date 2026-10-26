#pragma once

#include "game/GameMode.h"

#include <filesystem>

std::filesystem::path assetRoot();
std::filesystem::path levelDirectory(GameMode mode);
