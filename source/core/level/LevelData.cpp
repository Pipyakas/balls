#include "level/LevelData.h"

#include <algorithm>
#include <format>
#include <set>

const BrickType* BrickLayout::findType(int id) const {
    if (id >= 0 && static_cast<size_t>(id) < types.size() && types[static_cast<size_t>(id)].id == id) {
        return &types[static_cast<size_t>(id)];
    }
    const auto it = std::find_if(types.begin(), types.end(), [id](const BrickType& type) { return type.id == id; });
    return it == types.end() ? nullptr : &*it;
}

std::unordered_map<int, const BrickType*> BrickLayout::typesById() const {
    std::unordered_map<int, const BrickType*> index;
    index.reserve(types.size());
    for (const BrickType& type : types) {
        index.try_emplace(type.id, &type);
    }
    return index;
}

int countPlacedBricks(const BrickLayout& layout) {
    std::set<int> placedIds(layout.cellIds.begin(), layout.cellIds.end());
    placedIds.erase(emptyBrickId);
    return static_cast<int>(placedIds.size());
}

std::string levelNameWithBrickCount(std::string_view baseName, int bricks) {
    return std::format("{}_{}{}", baseName, bricks, bricks == 1 ? "Brick" : "Bricks");
}

ColorTally countBrickHitsByColor(const BrickLayout& layout) {
    ColorTally tally{};
    std::set<int> placedIds(layout.cellIds.begin(), layout.cellIds.end());
    for (const BrickType& type : layout.types) {
        if (placedIds.contains(type.id)) {
            tally[toIndex(type.color)] += type.hits;
        }
    }
    return tally;
}

ColorTally countBallsByColor(const LauncherLayout& layout) {
    ColorTally tally{};
    for (const auto& slot : layout.slots) {
        if (slot) {
            tally[toIndex(slot->color)] += slot->ballCount;
        }
    }
    return tally;
}

int countDistinctColors(const ColorTally& tally) {
    return static_cast<int>(std::count_if(tally.begin(), tally.end(), [](int count) { return count > 0; }));
}
