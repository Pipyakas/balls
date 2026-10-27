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
    // A physics backend name (see backendKindName); empty lets the front end pick. The machine benchmark writes its
    // winner here, and the player can override it from the Settings page.
    std::string defaultBackend;

    bool operator==(const Settings&) const = default;
};

std::string_view orientationName(LayoutOrientation orientation);
std::optional<LayoutOrientation> parseOrientation(std::string_view name);

std::string serializeSettings(const Settings& settings);
Settings parseSettings(std::string_view text);

Settings loadSettings(const std::filesystem::path& file);
Result<void> saveSettings(const std::filesystem::path& file, const Settings& settings);
