#include "app/PreferenceDirectory.h"

#include <SDL3/SDL_filesystem.h>

namespace {

// The organization and application names pick the per-user folder, so they stay as they were when the first builds
// shipped: changing them would strand the machine profile and settings of every existing install.
constexpr const char* organizationName = "Pipyakas";
constexpr const char* applicationName = "ColorBlastBalls";
constexpr const char* settingsFileName = "settings.txt";

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
