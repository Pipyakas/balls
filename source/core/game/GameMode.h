#pragma once

#include <optional>
#include <string_view>

enum class GameMode { Normal, Performance };

// "normal" or "performance" ("perf" too), in any case.
std::optional<GameMode> parseGameMode(std::string_view text);
std::string_view gameModeName(GameMode mode);
