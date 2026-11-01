#pragma once

#include "common/BallColor.h"
#include "level/GameplayRules.h"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

inline constexpr int emptyBrickId = -1;

struct BrickType {
    int id = emptyBrickId;
    BallColor color = BallColor::Red;
    int hits = 1;
};

struct BrickLayout {
    int width = 0;
    int height = 0;
    int playableHeight = 0;
    std::vector<int> cellIds;
    std::vector<BrickType> types;

    int cellId(int column, int row) const { return cellIds[static_cast<size_t>(row) * width + column]; }
    const BrickType* findType(int id) const;
    std::unordered_map<int, const BrickType*> typesById() const;
};

struct Launcher {
    BallColor color = BallColor::Red;
    int ballCount = 0;
};

struct LauncherLayout {
    int columns = 0;
    int visibleRows = 0;
    std::vector<std::optional<Launcher>> slots;

    int rowCount() const { return columns == 0 ? 0 : static_cast<int>(slots.size()) / columns; }
    const std::optional<Launcher>& slot(int column, int row) const {
        return slots[static_cast<size_t>(row) * columns + column];
    }
};

struct LevelData {
    std::string name;
    BrickLayout bricks;
    LauncherLayout launchers;
    GameplayRules rules;

    std::vector<std::string> repairs;
};

struct LevelFiles {
    std::string name;
    std::filesystem::path bricksPath;
    std::filesystem::path launchersPath;
};

using ColorTally = std::array<int, ballColorCount>;

ColorTally countBrickHitsByColor(const BrickLayout& layout);
ColorTally countBallsByColor(const LauncherLayout& layout);
int countDistinctColors(const ColorTally& tally);

int countPlacedBricks(const BrickLayout& layout);

std::string levelNameWithBrickCount(std::string_view baseName, int bricks);
