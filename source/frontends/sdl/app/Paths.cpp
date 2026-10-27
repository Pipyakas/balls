#include "app/Paths.h"

#include <SDL3/SDL_filesystem.h>

namespace {

std::filesystem::path executableDirectory() {
    const char* base = SDL_GetBasePath();
    return base ? std::filesystem::path(reinterpret_cast<const char8_t*>(base)) : std::filesystem::current_path();
}

}

std::filesystem::path assetRoot() {
    static const std::filesystem::path root = [] {
#if defined(BALLS_DEV_ASSET_DIR)
        if (std::filesystem::exists(std::filesystem::path(BALLS_DEV_ASSET_DIR) / "levels")) {
            return std::filesystem::path(BALLS_DEV_ASSET_DIR);
        }
#endif
        for (auto directory = executableDirectory(); !directory.empty(); directory = directory.parent_path()) {
            if (std::filesystem::exists(directory / "assets" / "levels")) {
                return directory / "assets";
            }
            if (directory == directory.parent_path()) {
                break;
            }
        }
        return std::filesystem::current_path() / "assets";
    }();
    return root;
}

std::filesystem::path levelDirectory(GameMode mode) {
    return assetRoot() / "levels" / (mode == GameMode::Normal ? "normal" : "performance");
}

std::filesystem::path fontPath(std::string_view fileName) { return assetRoot() / "fonts" / fileName; }

std::filesystem::path pathFromUtf8(const char* text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text)));
}

std::string utf8Of(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}
