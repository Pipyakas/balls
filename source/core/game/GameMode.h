#pragma once

#include <optional>
#include <string_view>

enum class GameMode { Normal, Performance };

std::optional<GameMode> parseGameMode(std::string_view text);
std::string_view gameModeName(GameMode mode);
