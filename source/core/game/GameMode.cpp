#include "game/GameMode.h"

#include <algorithm>
#include <cctype>
#include <string>

std::optional<GameMode> parseGameMode(std::string_view text) {
    std::string name(text);
    std::ranges::transform(name, name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (name == "normal") {
        return GameMode::Normal;
    }
    if (name == "performance" || name == "perf") {
        return GameMode::Performance;
    }
    return std::nullopt;
}

std::string_view gameModeName(GameMode mode) { return mode == GameMode::Normal ? "normal" : "performance"; }
