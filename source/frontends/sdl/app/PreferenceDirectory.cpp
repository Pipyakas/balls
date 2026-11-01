#include "app/PreferenceDirectory.h"

#include <SDL3/SDL_filesystem.h>

namespace {

constexpr const char* organizationName = "Pipyakas";
constexpr const char* applicationName = "balls";

constexpr const char* legacyApplicationName = "ColorBlastBalls";
constexpr const char* settingsFileName = "settings.txt";

void adoptLegacyFiles(const std::filesystem::path& directory) {
    std::error_code error;
    const std::filesystem::path legacy = directory.parent_path() / legacyApplicationName;
    if (!std::filesystem::is_directory(legacy, error)) {
        return;
    }
    for (const auto& entry : std::filesystem::directory_iterator(legacy, error)) {
        const std::filesystem::path target = directory / entry.path().filename();
        if (!std::filesystem::exists(target, error)) {
            std::filesystem::rename(entry.path(), target, error);
        }
    }
    std::filesystem::remove(legacy, error);
}

}

std::filesystem::path preferenceDirectory() {
    char* directory = SDL_GetPrefPath(organizationName, applicationName);
    if (!directory) {
        return std::filesystem::temp_directory_path() / applicationName;
    }
    const std::filesystem::path path(reinterpret_cast<const char8_t*>(directory));
    SDL_free(directory);
    adoptLegacyFiles(path.parent_path());
    return path;
}

std::filesystem::path settingsFilePath() { return preferenceDirectory() / settingsFileName; }
