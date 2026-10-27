#include "app/LaunchOptions.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <string>

namespace {

std::string lowercase(std::string_view text) {
    std::string result(text);
    std::ranges::transform(result, result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

int parseInteger(std::string_view text, int fallback) {
    int value = fallback;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

}

std::optional<GameMode> parseGameMode(std::string_view text) {
    const std::string name = lowercase(text);
    if (name == "normal") {
        return GameMode::Normal;
    }
    if (name == "performance" || name == "perf") {
        return GameMode::Performance;
    }
    return std::nullopt;
}

std::optional<BackendKind> parseBackendKind(std::string_view text) {
    const std::string name = lowercase(text);
    for (BackendKind kind : allBackendKinds) {
        if (lowercase(backendKindName(kind)) == name) {
            return kind;
        }
    }
    return std::nullopt;
}

LaunchOptions parseLaunchOptions(int argumentCount, char** arguments) {
    LaunchOptions options;
    if (const char* gpuEnvironment = std::getenv("BALLS_GPU")) {
        options.gpu = gpuEnvironment;
    }
    if (const char* computeGpuEnvironment = std::getenv("BALLS_COMPUTE_GPU")) {
        options.computeGpu = computeGpuEnvironment;
    }
    for (int i = 1; i < argumentCount; ++i) {
        const std::string_view argument = arguments[i];
        const bool hasValue = i + 1 < argumentCount;
        if (argument == "--play" && i + 2 < argumentCount) {
            options.playMode = parseGameMode(arguments[i + 1]);
            options.playLevel = arguments[i + 2];
            i += 2;
        } else if (argument == "--backend" && hasValue) {
            options.backend = parseBackendKind(arguments[++i]);
        } else if (argument == "--control-port" && hasValue) {
            options.controlPort = parseInteger(arguments[++i], 0);
        } else if (argument == "--size" && i + 2 < argumentCount) {
            options.windowWidth = parseInteger(arguments[i + 1], options.windowWidth);
            options.windowHeight = parseInteger(arguments[i + 2], options.windowHeight);
            i += 2;
        } else if (argument == "--api" && hasValue) {
            options.graphicsApi = lowercase(arguments[++i]);
        } else if (argument == "--gpu" && hasValue) {
            options.gpu = lowercase(arguments[++i]);
        } else if (argument == "--compute-gpu" && hasValue) {
            options.computeGpu = lowercase(arguments[++i]);
        } else if (argument == "--background") {
            options.background = true;
        } else if (argument == "--vsync") {
            options.vsync = true;
        } else if (argument == "--hidden") {
            options.hidden = true;
        }
    }
    return options;
}
