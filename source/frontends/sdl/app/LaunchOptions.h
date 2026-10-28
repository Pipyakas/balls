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
    // Which adapter renders: "auto" (default), "integrated", "discrete", an index, or a name substring.
    std::string gpu;
    // Which adapter runs GPU physics, same vocabulary as gpu. Empty means auto; it may differ from the renderer's.
    std::string computeGpu;
    std::optional<BackendKind> backend;
    int controlPort = 0;
    bool background = false;
    bool hidden = false;
    bool vsync = false;
    int windowWidth = 1440;
    int windowHeight = 900;
};

LaunchOptions parseLaunchOptions(int argumentCount, char** arguments);
