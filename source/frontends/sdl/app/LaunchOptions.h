#pragma once

#include "game/GameMode.h"
#include "sim/BackendFactory.h"

#include <optional>
#include <string>

struct LaunchOptions {
    std::optional<GameMode> playMode;
    std::string playLevel;
    std::string renderer;
    std::string graphicsApi;

    std::string gpu;

    std::string computeGpu;
    std::optional<BackendKind> backend;
    int controlPort = 0;
    bool background = false;
    bool hidden = false;
    bool vsync = false;
    bool editor = false;
    int windowWidth = 1440;
    int windowHeight = 900;
};

LaunchOptions parseLaunchOptions(int argumentCount, char** arguments);
