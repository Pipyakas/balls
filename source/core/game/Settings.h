#pragma once

#include "common/Result.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

enum class LayoutOrientation { Auto, Landscape, Portrait };

// Player-facing options shown on the Settings page.
// Front ends own where the file lives; core owns the format and the defaults.
struct Settings {
    // Ball-vs-ball collisions are a testing aid only: the game rules have balls pass through each other.
    bool ballCollisions = false;
    LayoutOrientation layout = LayoutOrientation::Auto;
    bool vsync = false;
    // Off skips drawing the board and its balls, so the physics can be measured on its own.
    bool drawBoard = true;
    // Physics ticks per second: 120 is the game; 240 and 480 check contacts more often, for accuracy on faster
    // hardware, at that much more work per second. Takes effect when a level starts.
    int ticksPerSecond = 120;
    // A physics backend name (see backendKindName); empty lets the front end pick. The machine benchmark writes its
    // winner here, and the player can override it from the Settings page.
    std::string defaultBackend;
    std::string renderer = "webgpu";
    std::string graphicsApi = "auto";

    bool operator==(const Settings&) const = default;
};

std::string_view orientationName(LayoutOrientation orientation);
std::optional<LayoutOrientation> parseOrientation(std::string_view name);

std::string normalizeRendererName(std::string_view renderer);
std::string normalizeGraphicsApiName(std::string_view api);

std::string serializeSettings(const Settings& settings);
Settings parseSettings(std::string_view text);

Settings loadSettings(const std::filesystem::path& file);
Result<void> saveSettings(const std::filesystem::path& file, const Settings& settings);
