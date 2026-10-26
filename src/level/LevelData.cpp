#include "level/LevelData.h"

#include <algorithm>
#include <set>

const BrickType* BrickLayout::findType(int id) const {
    const auto it = std::find_if(types.begin(), types.end(), [id](const BrickType& type) { return type.id == id; });
    return it == types.end() ? nullptr : &*it;
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
