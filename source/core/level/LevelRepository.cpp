#include "level/LevelRepository.h"

#include <algorithm>

std::vector<LevelFiles> findLevels(const std::filesystem::path& directory) {
    std::vector<LevelFiles> levels;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        const std::string fileName = entry.path().filename().string();
        if (!entry.is_regular_file() || !fileName.ends_with(bricksFileSuffix)) {
            continue;
        }
        const std::string name = fileName.substr(0, fileName.size() - bricksFileSuffix.size());
        LevelFiles files = levelFilesFor(directory, name);
        if (std::filesystem::exists(files.launchersPath)) {
            levels.push_back(std::move(files));
        }
    }
    std::ranges::sort(levels, {}, &LevelFiles::name);
    return levels;
}

LevelFiles levelFilesFor(const std::filesystem::path& directory, const std::string& name) {
    return {name, directory / (name + std::string(bricksFileSuffix)),
            directory / (name + std::string(launchersFileSuffix))};
}
