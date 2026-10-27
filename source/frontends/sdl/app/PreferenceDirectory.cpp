#include "app/PreferenceDirectory.h"

#include <SDL3/SDL_filesystem.h>

namespace {

constexpr const char* organizationName = "the company";
constexpr const char* applicationName = "ColorBlastBalls";
constexpr const char* settingsFileName = "settings.json";

} // namespace

std::filesystem::path preferenceDirectory() {
    char* directory = SDL_GetPrefPath(organizationName, applicationName);
    if (!directory) {
        return std::filesystem::temp_directory_path() / applicationName;
    }
    const std::filesystem::path path(reinterpret_cast<const char8_t*>(directory));
    SDL_free(directory);
    return path;
}

std::filesystem::path settingsFilePath() { return preferenceDirectory() / settingsFileName; }
