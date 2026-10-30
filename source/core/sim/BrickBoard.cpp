#include "sim/BrickBoard.h"

#include <algorithm>
#include <unordered_map>

namespace {

constexpr float riseSpeedCellsPerSecond = 10.0f;
constexpr int32_t emptyCell = -1;
// Brick ids up to this many times the number of types, plus the floor, are looked up in arrays.
constexpr size_t denseIdSlack = 4;
constexpr size_t denseIdFloor = 1 << 16;
constexpr uint32_t changeKindShift = 30;
constexpr uint32_t changeIndexMask = (1u << changeKindShift) - 1;
// Past this many unread entries the log is dropped; a follower that far behind copies the whole board instead.
constexpr size_t longestChangeLog = size_t{1} << 26;

}

void BrickBoard::load(const BrickLayout& layout) {
    columnCount = layout.width;
    rowCount = layout.playableHeight;
    cells.assign(static_cast<size_t>(paddedCellCount()), emptyCell);
    brickList.clear();
    colors.clear();
    remainingHits = {};
    image.reset(columnCount, rowCount);

    // Brick ids are almost always 0..N-1: then a brick's index and type are looked up in arrays, not in hash maps
    // of tens of millions of entries.
    int largestId = -1;
    bool negativeId = false;
    for (const BrickType& type : layout.types) {
        largestId = std::max(largestId, type.id);
        negativeId = negativeId || type.id < 0;
    }
    const bool denseIds =
        !negativeId && static_cast<size_t>(largestId) < layout.types.size() * denseIdSlack + denseIdFloor;
    std::vector<const BrickType*> typeById;
    std::vector<int> indexById;
    std::unordered_map<int, const BrickType*> sparseTypes;
    std::unordered_map<int, int> sparseIndices;
    if (denseIds) {
        typeById.assign(static_cast<size_t>(largestId) + 1, nullptr);
        indexById.assign(static_cast<size_t>(largestId) + 1, emptyCell);
        for (const BrickType& type : layout.types) {
            if (!typeById[static_cast<size_t>(type.id)]) {
                typeById[static_cast<size_t>(type.id)] = &type;
            }
        }
    } else {
        sparseTypes = layout.typesById();
    }
    const auto typeOf = [&](int id) -> const BrickType* {
        if (id == emptyBrickId) {
            return nullptr;
        }
        if (denseIds) {
            return id >= 0 && id <= largestId ? typeById[static_cast<size_t>(id)] : nullptr;
        }
        const auto entry = sparseTypes.find(id);
        return entry == sparseTypes.end() ? nullptr : entry->second;
    };
    const auto indexSlot = [&](int id) -> int& {
        return denseIds ? indexById[static_cast<size_t>(id)] : sparseIndices.try_emplace(id, emptyCell).first->second;
    };
    brickList.reserve(layout.types.size());
    colors.reserve(layout.types.size());
    for (int row = 0; row < layout.height; ++row) {
        for (int column = 0; column < layout.width; ++column) {
            const int id = layout.cellId(column, row);
            const BrickType* type = typeOf(id);
            if (!type) {
                continue;
            }
            int& index = indexSlot(id);
            if (index == emptyCell) {
                index = static_cast<int>(brickList.size());
                brickList.push_back({id, type->color, type->hits, type->hits, {}, 0.0f, 0.0f});
                colors.push_back(toIndex(type->color));
                remainingHits[toIndex(type->color)] += type->hits;
            }
            brickList[static_cast<size_t>(index)].cells.push_back({column, row});
            cells[paddedIndex(column, row)] = index;
            image.set(column, row, shadeOf(index));
        }
    }
    aliveCount = static_cast<int>(brickList.size());
    aliveBricks.resize(brickList.size());
    alivePosition.resize(brickList.size());
    for (int index = 0; index < aliveCount; ++index) {
        aliveBricks[static_cast<size_t>(index)] = index;
        alivePosition[static_cast<size_t>(index)] = index;
    }
    animatingBricks.clear();
    brickAnimating.assign(brickList.size(), 0);
    bricksToSettle = {};
    rebuildNearCounts();
    // The first settle orders every brick by height, 170 ms on a 4M-brick board; most layouts have nothing that can
    // rise, so the board only asks for it when some brick has room above.
    wholeBoardSettlePending = false;
    for (int index = 0; index < aliveCount && !wholeBoardSettlePending; ++index) {
        wholeBoardSettlePending = freeRowsAbove(index) > 0;
    }
    cellChanged.assign(cells.size(), 0);
    dirtyCells.clear();
    ++changeRevision;
    changeLogBase += changeLog.size();
    changeLog.clear();
}

void BrickBoard::rebuildNearCounts() {
    nearCounts.assign(cells.size(), 0);
    nearFlags.assign(cells.size(), 0);
    const int width = paddedWidth();
    const int height = rowCount + 2;
    for (int row = 1; row < height - 1; ++row) {
        for (int column = 1; column < width - 1; ++column) {
            if (cells[static_cast<size_t>(row) * width + column] == emptyCell) {
                continue;
            }
            for (int nearRow = row - 1; nearRow <= row + 1; ++nearRow) {
                for (int nearColumn = column - 1; nearColumn <= column + 1; ++nearColumn) {
                    const size_t near = static_cast<size_t>(nearRow) * width + nearColumn;
                    ++nearCounts[near];
                    nearFlags[near] = 1;
                }
            }
        }
    }
}

void BrickBoard::clearChangedCells() {
    for (const int32_t index : dirtyCells) {
        cellChanged[static_cast<size_t>(index)] = 0;
    }
    dirtyCells.clear();
}

void BrickBoard::logChange(LoggedChange kind, int index) {
    if (!logging) {
        return;
    }
    if (changeLog.size() >= longestChangeLog) {
        changeLogBase += changeLog.size();
        changeLog.clear();
    }
    changeLog.push_back((static_cast<uint32_t>(kind) << changeKindShift) | static_cast<uint32_t>(index));
}

void BrickBoard::trimChangeLog(uint64_t keepFrom) const {
    logging = true;
    if (keepFrom <= changeLogBase) {
        return;
    }
    const size_t dropped = static_cast<size_t>(std::min<uint64_t>(keepFrom - changeLogBase, changeLog.size()));
    changeLog.erase(changeLog.begin(), changeLog.begin() + static_cast<std::ptrdiff_t>(dropped));
    changeLogBase += dropped;
}

// Everything the UI reads; the physics-only state (near-brick counts, changed cells, the settle queue) stays empty.
void BrickBoard::copyForView(const BrickBoard& live) {
    columnCount = live.columnCount;
    rowCount = live.rowCount;
    cells = live.cells;
    colors = live.colors;
    brickList = live.brickList;
    aliveCount = live.aliveCount;
    aliveBricks = live.aliveBricks;
    alivePosition = live.alivePosition;
    brickAnimating = live.brickAnimating;
    image = live.image;
}

void BrickBoard::catchUpWith(const BrickBoard& live) {
    live.logging = true;
    const bool sameBoard = following && layoutSerial() == live.layoutSerial() && followed >= live.changeLogBase;
    if (!sameBoard) {
        copyForView(live);
    } else {
        for (uint64_t position = followed; position < live.changeLogEnd(); ++position) {
            const uint32_t entry = live.changeLog[static_cast<size_t>(position - live.changeLogBase)];
            const int index = static_cast<int>(entry & changeIndexMask);
            switch (static_cast<LoggedChange>(entry >> changeKindShift)) {
            case LoggedChange::Brick:
                brickList[static_cast<size_t>(index)] = live.brickList[static_cast<size_t>(index)];
                brickAnimating[static_cast<size_t>(index)] = live.brickAnimating[static_cast<size_t>(index)];
                break;
            case LoggedChange::Cell:
                cells[static_cast<size_t>(index)] = live.cells[static_cast<size_t>(index)];
                break;
            case LoggedChange::Death:
                markDead(index);
                break;
            }
        }
        image.catchUpWith(live.image);
        aliveCount = live.aliveCount;
    }
    animatingBricks = live.animatingBricks;
    remainingHits = live.remainingHits;
    changeRevision = live.changeRevision;
    followed = live.changeLogEnd();
    following = true;
}

void BrickBoard::markCellChanged(int index) {
    if (!cellChanged[static_cast<size_t>(index)]) {
        cellChanged[static_cast<size_t>(index)] = 1;
        dirtyCells.push_back(index);
    }
}

void BrickBoard::setCell(int index, int32_t value) {
    const int32_t previous = cells[static_cast<size_t>(index)];
    if (previous == value) {
        return;
    }
    cells[static_cast<size_t>(index)] = value;
    markCellChanged(index);
    logChange(LoggedChange::Cell, index);
    const int width = paddedWidth();
    image.set(index % width - 1, index / width - 1, value == emptyCell ? BrickCellImage::emptyShade : shadeOf(value));
    if ((previous == emptyCell) == (value == emptyCell)) {
        return;
    }
    const int delta = value == emptyCell ? -1 : 1;
    for (const int near : {index - width - 1, index - width, index - width + 1, index - 1, index, index + 1,
                           index + width - 1, index + width, index + width + 1}) {
        const int count = nearCounts[static_cast<size_t>(near)] += delta;
        const int32_t flag = count > 0 ? 1 : 0;
        if (nearFlags[static_cast<size_t>(near)] != flag) {
            nearFlags[static_cast<size_t>(near)] = flag;
            markCellChanged(near);
        }
    }
}

bool BrickBoard::applyHit(int brickIndex) {
    Brick& brick = brickList[brickIndex];
    if (!brick.isAlive()) {
        return false;
    }
    --brick.hitsLeft;
    --remainingHits[toIndex(brick.color)];
    const bool wasFlashing = brick.hitFlash > 0.0f;
    brick.hitFlash = brickHitFlashSeconds;
    startAnimating(brickIndex);
    logChange(LoggedChange::Brick, brickIndex);
    if (brick.isAlive()) {
        if (!wasFlashing) {
            reshadeBrick(brickIndex);
        }
        return true;
    }
    brick.breakBurst = brickBreakBurstSeconds;
    placeBrick(brickIndex, emptyCell);
    queueBricksBelow(brickIndex);
    --aliveCount;
    markDead(brickIndex);
    ++changeRevision;
    return true;
}

bool BrickBoard::applyHitsDownTo(int brickIndex, int hitsLeft) {
    bool changed = false;
    while (brickList[brickIndex].hitsLeft > std::max(hitsLeft, 0)) {
        changed = applyHit(brickIndex) || changed;
    }
    return changed;
}

std::vector<int32_t> BrickBoard::hitsLeftPerBrick() const {
    std::vector<int32_t> hits;
    hits.reserve(brickList.size());
    for (const Brick& brick : brickList) {
        hits.push_back(brick.hitsLeft);
    }
    return hits;
}

uint8_t BrickBoard::shadeOf(int brickIndex) const {
    return BrickCellImage::brickShade(colors[static_cast<size_t>(brickIndex)],
                                      brickList[static_cast<size_t>(brickIndex)].hitFlash > 0.0f);
}

void BrickBoard::reshadeBrick(int brickIndex) {
    const uint8_t shade = shadeOf(brickIndex);
    for (const BrickCell& cell : brickList[static_cast<size_t>(brickIndex)].cells) {
        image.set(cell.column, cell.row, shade);
    }
}

void BrickBoard::placeBrick(int brickIndex, int32_t value) {
    for (const BrickCell& cell : brickList[brickIndex].cells) {
        setCell(paddedIndex(cell.column, cell.row), value);
    }
}

int BrickBoard::freeRowsAbove(int brickIndex) const {
    int distance = rowCount;
    for (const BrickCell& cell : brickList[brickIndex].cells) {
        int free = 0;
        for (int row = cell.row - 1; row >= 0 && free < distance; --row) {
            const int above = brickAt(cell.column, row);
            if (above == brickIndex) {
                free = distance;
                break;
            }
            if (above != emptyCell) {
                break;
            }
            ++free;
        }
        distance = std::min(distance, free);
        if (distance == 0) {
            return 0;
        }
    }
    return distance;
}

bool BrickBoard::raiseFully(int brickIndex) {
    const int distance = freeRowsAbove(brickIndex);
    if (distance == 0) {
        return false;
    }
    Brick& brick = brickList[brickIndex];
    placeBrick(brickIndex, emptyCell);
    for (BrickCell& cell : brick.cells) {
        cell.row -= distance;
    }
    placeBrick(brickIndex, brickIndex);
    brick.riseOffset += static_cast<float>(distance);
    startAnimating(brickIndex);
    logChange(LoggedChange::Brick, brickIndex);
    return true;
}

int BrickBoard::lowestRowInColumn(int brickIndex, int column) const {
    int lowest = -1;
    for (const BrickCell& cell : brickList[brickIndex].cells) {
        if (cell.column == column) {
            lowest = std::max(lowest, cell.row);
        }
    }
    return lowest;
}

void BrickBoard::queueFirstBrickBelow(int column, int row) {
    for (int below = row + 1; below < rowCount; ++below) {
        const int brick = brickAt(column, below);
        if (brick != emptyCell) {
            bricksToSettle.emplace(below, brick);
            return;
        }
    }
}

void BrickBoard::queueBricksBelow(int brickIndex) {
    for (const BrickCell& cell : brickList[brickIndex].cells) {
        if (lowestRowInColumn(brickIndex, cell.column) == cell.row) {
            queueFirstBrickBelow(cell.column, cell.row);
        }
    }
}

bool BrickBoard::settleWholeBoard() {
    std::vector<int> order;
    order.reserve(brickList.size());
    for (int index = 0; index < static_cast<int>(brickList.size()); ++index) {
        if (brickList[index].isAlive()) {
            order.push_back(index);
        }
    }
    const auto topRow = [this](int index) {
        return std::ranges::min(brickList[index].cells, {}, &BrickCell::row).row;
    };
    std::ranges::sort(order, {}, topRow);
    bool anyMoved = false;
    for (const int brick : order) {
        if (raiseFully(brick)) {
            anyMoved = true;
            queueBricksBelow(brick);
        }
    }
    return anyMoved;
}

bool BrickBoard::settleColumns() {
    bool anyMoved = false;
    if (wholeBoardSettlePending) {
        wholeBoardSettlePending = false;
        anyMoved = settleWholeBoard();
    }
    while (!bricksToSettle.empty()) {
        const int brick = bricksToSettle.top().second;
        bricksToSettle.pop();
        if (brickList[brick].isAlive() && raiseFully(brick)) {
            anyMoved = true;
            queueBricksBelow(brick);
        }
    }
    if (anyMoved) {
        ++changeRevision;
    }
    return anyMoved;
}

void BrickBoard::animate(float deltaTime) {
    size_t kept = 0;
    for (const int index : animatingBricks) {
        Brick& brick = brickList[static_cast<size_t>(index)];
        brick.riseOffset = std::max(0.0f, brick.riseOffset - riseSpeedCellsPerSecond * deltaTime);
        const bool wasFlashing = brick.hitFlash > 0.0f;
        brick.hitFlash = std::max(0.0f, brick.hitFlash - deltaTime);
        if (wasFlashing && brick.hitFlash == 0.0f && brick.isAlive()) {
            reshadeBrick(index);
        }
        brick.breakBurst = std::max(0.0f, brick.breakBurst - deltaTime);
        if (brick.riseOffset > 0.0f || brick.hitFlash > 0.0f || brick.breakBurst > 0.0f) {
            animatingBricks[kept++] = index;
        } else {
            brickAnimating[static_cast<size_t>(index)] = 0;
        }
        logChange(LoggedChange::Brick, index);
    }
    animatingBricks.resize(kept);
}

void BrickBoard::startAnimating(int brickIndex) {
    if (!brickAnimating[static_cast<size_t>(brickIndex)]) {
        brickAnimating[static_cast<size_t>(brickIndex)] = 1;
        animatingBricks.push_back(brickIndex);
    }
}

void BrickBoard::markDead(int brickIndex) {
    logChange(LoggedChange::Death, brickIndex);
    const int position = alivePosition[static_cast<size_t>(brickIndex)];
    const int last = aliveBricks.back();
    aliveBricks[static_cast<size_t>(position)] = last;
    alivePosition[static_cast<size_t>(last)] = position;
    aliveBricks.pop_back();
}
