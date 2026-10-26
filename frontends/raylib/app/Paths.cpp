#include "app/Paths.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace {

std::filesystem::path executableDirectory() {
    wchar_t buffer[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(buffer).parent_path();
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
