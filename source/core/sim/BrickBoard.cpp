#include "sim/BrickBoard.h"

#include <algorithm>
#include <unordered_map>

namespace {

constexpr float riseSpeedCellsPerSecond = 10.0f;
constexpr int32_t emptyCell = -1;

}

void BrickBoard::load(const BrickLayout& layout) {
    columnCount = layout.width;
    rowCount = layout.playableHeight;
    cells.assign(static_cast<size_t>(paddedCellCount()), emptyCell);
    brickList.clear();
    colors.clear();
    remainingHits = {};

    std::unordered_map<int, int> brickIndexById;
    const auto typesById = layout.typesById();
    for (int row = 0; row < layout.height; ++row) {
        for (int column = 0; column < layout.width; ++column) {
            const int id = layout.cellId(column, row);
            const auto typeEntry = id == emptyBrickId ? typesById.end() : typesById.find(id);
            if (typeEntry == typesById.end()) {
                continue;
            }
            const BrickType* type = typeEntry->second;
            auto [it, inserted] = brickIndexById.try_emplace(id, static_cast<int>(brickList.size()));
            if (inserted) {
                brickList.push_back({id, type->color, type->hits, type->hits, {}, 0.0f, 0.0f});
                colors.push_back(toIndex(type->color));
                remainingHits[toIndex(type->color)] += type->hits;
            }
            brickList[it->second].cells.push_back({column, row});
            cells[paddedIndex(column, row)] = it->second;
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
    wholeBoardSettlePending = true;
    rebuildNearCounts();
    cellChanged.assign(cells.size(), 0);
    dirtyCells.clear();
    ++changeRevision;
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
    if ((previous == emptyCell) == (value == emptyCell)) {
        return;
    }
    const int delta = value == emptyCell ? -1 : 1;
    const int width = paddedWidth();
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
    brick.hitFlash = brickHitFlashSeconds;
    startAnimating(brickIndex);
    if (brick.isAlive()) {
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
        brick.hitFlash = std::max(0.0f, brick.hitFlash - deltaTime);
        brick.breakBurst = std::max(0.0f, brick.breakBurst - deltaTime);
        if (brick.riseOffset > 0.0f || brick.hitFlash > 0.0f || brick.breakBurst > 0.0f) {
            animatingBricks[kept++] = index;
        } else {
            brickAnimating[static_cast<size_t>(index)] = 0;
        }
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
    const int position = alivePosition[static_cast<size_t>(brickIndex)];
    const int last = aliveBricks.back();
    aliveBricks[static_cast<size_t>(position)] = last;
    alivePosition[static_cast<size_t>(last)] = position;
    aliveBricks.pop_back();
}
