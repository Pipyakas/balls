#include "level/LevelWriter.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <set>

namespace {

Result<void> writeTextFileAtomically(const std::filesystem::path& path, const std::string& text) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    const auto temporaryPath = std::filesystem::path(path).concat(".tmp");
    {
        std::ofstream file(temporaryPath, std::ios::binary | std::ios::trunc);
        if (!file) {
            return Error{std::format("cannot write {}", temporaryPath.string())};
        }
        file << text;
    }
    std::filesystem::rename(temporaryPath, path, error);
    if (error) {
        return Error{std::format("cannot replace {}: {}", path.string(), error.message())};
    }
    return {};
}

}

std::string formatBrickLayout(const BrickLayout& layout) {
    std::string text = std::format("[Board]\nWidth {}\nHeight {}\nPlayableHeight {}\n\n[Layout]\n", layout.width,
                                   layout.height, layout.playableHeight);
    for (int row = 0; row < layout.height; ++row) {
        for (int column = 0; column < layout.width; ++column) {
            const int id = layout.cellId(column, row);
            text += column == 0 ? "" : " ";
            text += id == emptyBrickId ? std::string("-1") : std::to_string(id);
        }
        text += '\n';
    }
    text += "\n[BrickTypes]\n";
    const std::set<int> placedIds(layout.cellIds.begin(), layout.cellIds.end());
    auto types = layout.types;
    std::ranges::sort(types, {}, &BrickType::id);
    for (const BrickType& type : types) {
        if (placedIds.contains(type.id)) {
            text += std::format("{} {} {}\n", type.id, ballColorName(type.color), type.hits);
        }
    }
    return text;
}

std::string formatLauncherLayout(const LauncherLayout& layout) {
    std::string text =
        std::format("[Board]\nWidth {}\nHeight {}\n\n[Layout]\n", layout.columns, layout.visibleRows);
    for (int row = 0; row < layout.rowCount(); ++row) {
        for (int column = 0; column < layout.columns; ++column) {
            const auto& slot = layout.slot(column, row);
            text += column == 0 ? "" : " ";
            text += slot ? std::format("{}({})", ballColorName(slot->color), slot->ballCount) : std::string("Empty");
        }
        text += '\n';
    }
    return text;
}

Result<void> saveLevel(const LevelData& level, const LevelFiles& files) {
    if (auto result = writeTextFileAtomically(files.bricksPath, formatBrickLayout(level.bricks)); !result) {
        return result;
    }
    return writeTextFileAtomically(files.launchersPath, formatLauncherLayout(level.launchers));
}
