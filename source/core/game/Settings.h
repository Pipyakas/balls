#pragma once

#include "common/Result.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

enum class LayoutOrientation { Auto, Landscape, Portrait };

// The target PC has a landscape monitor, so the scoring build has no Layout choice: the game always lays out in
// landscape, and the window keeps the size it was given. A saved layout is kept in the file but not used.
#if defined(BALLS_FIXED_TARGET)
inline constexpr bool layoutChoiceOffered = false;
#else
inline constexpr bool layoutChoiceOffered = true;
#endif

struct Settings;
// The orientation the gameplay and editor layouts use, and the one the window is sized for (Auto: left alone).
LayoutOrientation layoutInUse(const Settings& settings);
LayoutOrientation windowLayout(const Settings& settings);

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
    std::string renderer = "webgpu";
    std::string graphicsApi = "auto";
    // Whether the gameplay control gadget shows its Advanced Settings section (backend and GPU choices), and with it
    // the backend and device rows of the debug gadget.
    bool advancedSettings = false;

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
