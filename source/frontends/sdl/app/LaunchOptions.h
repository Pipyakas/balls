#pragma once

#include "game/GameMode.h"
#include "sim/BackendFactory.h"

#include <optional>
#include <string>

struct LaunchOptions {
    std::optional<GameMode> playMode;
    std::string playLevel;
    std::string graphicsApi;
    std::optional<BackendKind> backend;
    int controlPort = 0;
    bool background = false;
    bool hidden = false;
    bool skipBenchmark = false;
    bool vsync = false;
    int windowWidth = 1440;
    int windowHeight = 900;
};

LaunchOptions parseLaunchOptions(int argumentCount, char** arguments);
std::optional<GameMode> parseGameMode(std::string_view text);
std::optional<BackendKind> parseBackendKind(std::string_view text);
