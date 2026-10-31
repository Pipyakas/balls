#pragma once

#include "common/Result.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

enum class LayoutOrientation { Auto, Landscape, Portrait };
// A landscape window wider than 16:9 either spreads the gameplay screen to its edges, the board's section taking the
// extra width, or keeps the screen a centred 16:9 group with dark bands at both sides.
enum class WideScreenAnchor { ScreenEdges, Centered };

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
    WideScreenAnchor wideScreen = WideScreenAnchor::ScreenEdges;
    bool vsync = false;
    // Off skips drawing the board and its balls, so the physics can be measured on its own.
    bool drawBoard = true;
    // Physics ticks per second: 120 is the game; 240 and 480 check contacts more often, for accuracy on faster
    // hardware, at that much more work per second. Takes effect when a level starts.
    int ticksPerSecond = 120;
    // How often the physics thread runs a frame and publishes it: 0 is one frame per tick, otherwise frames per
    // second, each running the ticks the fixed tick rate owes. Below the tick rate (60 on a 60 Hz screen) a frame
    // runs two ticks and draws once, so no picture is made that the screen would not show.
    int physicsFramesPerSecond = 0;
    // A physics backend name (see backendKindName); empty lets the front end pick. The machine benchmark writes its
    // winner here, and the player can override it from the Settings page.
    std::string defaultBackend;
    std::string renderer = "webgpu";
    std::string graphicsApi = "auto";
    // Whether the gameplay control gadget shows its Advanced Settings section (backend and GPU choices), and with it
    // the backend and device rows of the debug gadget.
    bool advancedSettings = false;
    // Off, a Performance launcher releases exactly the balls its level gives it, as every other entry does; on, the
    // Ball count multiplier (x1 to x10000) scales them. Shown under Advanced Settings in the Performance controls.
    bool overrideLauncherCounts = false;

    bool operator==(const Settings&) const = default;
};

std::string_view orientationName(LayoutOrientation orientation);
std::optional<LayoutOrientation> parseOrientation(std::string_view name);
std::string_view wideScreenAnchorName(WideScreenAnchor anchor);
std::optional<WideScreenAnchor> parseWideScreenAnchor(std::string_view name);

std::string normalizeRendererName(std::string_view renderer);
std::string normalizeGraphicsApiName(std::string_view api);

std::string serializeSettings(const Settings& settings);
Settings parseSettings(std::string_view text);

Settings loadSettings(const std::filesystem::path& file);
Result<void> saveSettings(const std::filesystem::path& file, const Settings& settings);
