#include "game/Settings.h"

#include <algorithm>
#include <charconv>
#include <array>
#include <cctype>
#include <format>
#include <fstream>
#include <sstream>
#include <utility>

namespace {

constexpr std::string_view collisionsKey = "ballCollisions";
constexpr std::string_view layoutKey = "layout";
constexpr std::string_view vsyncKey = "vsync";
constexpr std::string_view drawBoardKey = "drawBoard";
constexpr std::string_view ticksPerSecondKey = "physicsRate";
constexpr std::string_view defaultBackendKey = "defaultBackend";
constexpr std::string_view rendererKey = "renderer";
constexpr std::string_view graphicsApiKey = "graphicsApi";
constexpr std::string_view advancedSettingsKey = "advancedSettings";

constexpr std::array<std::pair<LayoutOrientation, std::string_view>, 3> orientationNames{{
    {LayoutOrientation::Auto, "auto"},
    {LayoutOrientation::Landscape, "landscape"},
    {LayoutOrientation::Portrait, "portrait"},
}};

std::string lowercase(std::string_view text) {
    std::string result(text);
    std::ranges::transform(result, result.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

std::string_view trim(std::string_view text) {
    const size_t first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) {
        return {};
    }
    const size_t last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
}

std::optional<bool> parseFlag(std::string_view value) {
    if (value == "1" || value == "true" || value == "on") {
        return true;
    }
    if (value == "0" || value == "false" || value == "off") {
        return false;
    }
    return std::nullopt;
}

} // namespace

std::string_view orientationName(LayoutOrientation orientation) {
    for (const auto& [value, name] : orientationNames) {
        if (value == orientation) {
            return name;
        }
    }
    return orientationNames.front().second;
}

std::optional<LayoutOrientation> parseOrientation(std::string_view name) {
    for (const auto& [value, known] : orientationNames) {
        if (known == name) {
            return value;
        }
    }
    return std::nullopt;
}

std::string normalizeRendererName(std::string_view renderer) {
    const std::string lower = lowercase(trim(renderer));
    if (lower == "nvrhi") {
        return "nvrhi";
    }
    if (lower == "sdlgpu") {
        return "sdlgpu";
    }
    return "webgpu";
}

std::string normalizeGraphicsApiName(std::string_view api) {
    const std::string lower = lowercase(trim(api));
    if (lower == "d3d12" || lower == "direct3d 12" || lower == "dx12") {
        return "d3d12";
    }
    if (lower == "vulkan" || lower == "vk") {
        return "vulkan";
    }
    return "auto";
}

std::string serializeSettings(const Settings& settings) {
    return std::format("{}={}\n{}={}\n{}={}\n{}={}\n{}={}\n{}={}\n{}={}\n{}={}\n{}={}\n",
                       collisionsKey, settings.ballCollisions ? "on" : "off",
                       layoutKey, orientationName(settings.layout),
                       vsyncKey, settings.vsync ? "on" : "off",
                       drawBoardKey, settings.drawBoard ? "on" : "off",
                       ticksPerSecondKey, settings.ticksPerSecond,
                       defaultBackendKey, settings.defaultBackend,
                       rendererKey, normalizeRendererName(settings.renderer),
                       graphicsApiKey, normalizeGraphicsApiName(settings.graphicsApi),
                       advancedSettingsKey, settings.advancedSettings ? "on" : "off");
}

// Unknown keys and unreadable values keep their defaults, so an old or hand-edited file never blocks startup.
Settings parseSettings(std::string_view text) {
    Settings settings;
    while (!text.empty()) {
        const size_t lineEnd = text.find('\n');
        const std::string_view line = text.substr(0, lineEnd);
        text = lineEnd == std::string_view::npos ? std::string_view{} : text.substr(lineEnd + 1);

        const size_t separator = line.find('=');
        if (separator == std::string_view::npos) {
            continue;
        }
        const std::string_view key = trim(line.substr(0, separator));
        const std::string_view value = trim(line.substr(separator + 1));
        if (key == collisionsKey) {
            settings.ballCollisions = parseFlag(value).value_or(settings.ballCollisions);
        } else if (key == layoutKey) {
            settings.layout = parseOrientation(value).value_or(settings.layout);
        } else if (key == vsyncKey) {
            settings.vsync = parseFlag(value).value_or(settings.vsync);
        } else if (key == drawBoardKey) {
            settings.drawBoard = parseFlag(value).value_or(settings.drawBoard);
        } else if (key == ticksPerSecondKey) {
            int rate = 0;
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), rate);
            if (error == std::errc{} && rate >= 30 && rate <= 1920) {
                settings.ticksPerSecond = rate;
            }
        } else if (key == defaultBackendKey) {
            settings.defaultBackend = std::string(value);
        } else if (key == rendererKey) {
            settings.renderer = normalizeRendererName(value);
        } else if (key == graphicsApiKey || key == "api") {
            settings.graphicsApi = normalizeGraphicsApiName(value);
        } else if (key == advancedSettingsKey) {
            settings.advancedSettings = parseFlag(value).value_or(settings.advancedSettings);
        }
    }
    return settings;
}

Settings loadSettings(const std::filesystem::path& file) {
    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        return {};
    }
    std::ostringstream contents;
    contents << stream.rdbuf();
    return parseSettings(contents.str());
}

Result<void> saveSettings(const std::filesystem::path& file, const Settings& settings) {
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    std::ofstream stream(file, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return Error{std::format("could not open settings file for write: {}", file.string())};
    }
    stream << serializeSettings(settings);
    if (!stream) {
        return Error{std::format("failed writing settings to {}", file.string())};
    }
    return {};
}
