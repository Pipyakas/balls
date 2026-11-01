#pragma once

#include "common/Result.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

enum class LayoutOrientation { Auto, Landscape, Portrait };

enum class WideScreenAnchor { ScreenEdges, Centered };

inline constexpr bool layoutChoiceOffered = true;

struct Settings;

LayoutOrientation layoutInUse(const Settings& settings);
LayoutOrientation windowLayout(const Settings& settings);

struct Settings {

    bool ballCollisions = false;
    LayoutOrientation layout = LayoutOrientation::Auto;
    WideScreenAnchor wideScreen = WideScreenAnchor::ScreenEdges;
    bool vsync = false;

    bool drawBoard = true;

    int ticksPerSecond = 120;

    int physicsFramesPerSecond = 0;

    std::string defaultBackend;
    std::string renderer = "webgpu";
    std::string graphicsApi = "auto";

    bool advancedSettings = false;

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
