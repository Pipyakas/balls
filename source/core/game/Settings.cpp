#include "game/Settings.h"

#include <array>
#include <format>
#include <fstream>
#include <sstream>
#include <utility>

namespace {

constexpr std::string_view collisionsKey = "ballCollisions";
constexpr std::string_view layoutKey = "layout";
constexpr std::string_view vsyncKey = "vsync";

constexpr std::array<std::pair<LayoutOrientation, std::string_view>, 3> orientationNames{{
    {LayoutOrientation::Auto, "auto"},
    {LayoutOrientation::Landscape, "landscape"},
    {LayoutOrientation::Portrait, "portrait"},
}};

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

std::string serializeSettings(const Settings& settings) {
    return std::format("{}={}\n{}={}\n{}={}\n", collisionsKey, settings.ballCollisions ? "on" : "off",
                       layoutKey, orientationName(settings.layout), vsyncKey, settings.vsync ? "on" : "off");
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
        return Error{std::format("cannot write {}", file.string())};
    }
    stream << serializeSettings(settings);
    return {};
}
