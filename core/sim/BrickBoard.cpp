#include "sim/BrickBoard.h"

#include <algorithm>
#include <map>
#include <numeric>

namespace {

constexpr float riseSpeedCellsPerSecond = 10.0f;
constexpr float hitFlashSeconds = 0.15f;
constexpr int32_t emptyCell = -1;

}

void BrickBoard::load(const BrickLayout& layout) {
    columnCount = layout.width;
    rowCount = layout.playableHeight;
    cells.assign(static_cast<size_t>(paddedCellCount()), emptyCell);
    brickList.clear();
    colors.clear();
    remainingHits = {};

    std::map<int, int> brickIndexById;
    for (int row = 0; row < layout.height; ++row) {
        for (int column = 0; column < layout.width; ++column) {
            const int id = layout.cellId(column, row);
            const BrickType* type = id == emptyBrickId ? nullptr : layout.findType(id);
            if (!type) {
                continue;
            }
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
    ++changeRevision;
}

bool BrickBoard::applyHit(int brickIndex) {
    Brick& brick = brickList[brickIndex];
    if (!brick.isAlive()) {
        return false;
    }
    --brick.hitsLeft;
    --remainingHits[toIndex(brick.color)];
    brick.hitFlash = hitFlashSeconds;
    if (brick.isAlive()) {
        return true;
    }
    placeBrick(brickIndex, emptyCell);
    --aliveCount;
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

std::vector<int32_t> BrickBoard::nearBrickFlags() const {
    std::vector<int32_t> flags(cells.size(), 0);
    const int width = paddedWidth();
    const int height = rowCount + 2;
    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
            if (cells[static_cast<size_t>(row) * width + column] == emptyCell) {
                continue;
            }
            for (int nearRow = std::max(row - 1, 0); nearRow <= std::min(row + 1, height - 1); ++nearRow) {
                for (int nearColumn = std::max(column - 1, 0); nearColumn <= std::min(column + 1, width - 1); ++nearColumn) {
                    flags[static_cast<size_t>(nearRow) * width + nearColumn] = 1;
                }
            }
        }
    }
    return flags;
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
        cells[paddedIndex(cell.column, cell.row)] = value;
    }
}

bool BrickBoard::tryRaise(int brickIndex) {
    Brick& brick = brickList[brickIndex];
    for (const BrickCell& cell : brick.cells) {
        if (cell.row == 0) {
            return false;
        }
        const int above = brickAt(cell.column, cell.row - 1);
        if (above != emptyCell && above != brickIndex) {
            return false;
        }
    }
    placeBrick(brickIndex, emptyCell);
    for (BrickCell& cell : brick.cells) {
        --cell.row;
    }
    placeBrick(brickIndex, brickIndex);
    brick.riseOffset += 1.0f;
    return true;
}

bool BrickBoard::settleColumns() {
    std::vector<int> order(brickList.size());
    std::iota(order.begin(), order.end(), 0);
    std::erase_if(order, [this](int index) { return !brickList[index].isAlive(); });
    auto topRow = [this](int index) {
        return std::ranges::min(brickList[index].cells, {}, &BrickCell::row).row;
    };
    std::ranges::sort(order, {}, topRow);

    bool anyMoved = false;
    bool movedThisPass = true;
    while (movedThisPass) {
        movedThisPass = false;
        for (int index : order) {
            while (tryRaise(index)) {
                movedThisPass = true;
            }
        }
        anyMoved = anyMoved || movedThisPass;
    }
    if (anyMoved) {
        ++changeRevision;
    }
    return anyMoved;
}

void BrickBoard::animate(float deltaTime) {
    for (Brick& brick : brickList) {
        brick.riseOffset = std::max(0.0f, brick.riseOffset - riseSpeedCellsPerSecond * deltaTime);
        brick.hitFlash = std::max(0.0f, brick.hitFlash - deltaTime);
    }
}
