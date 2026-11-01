#pragma once

#include "level/LevelData.h"
#include "sim/BrickCellImage.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <queue>
#include <utility>
#include <vector>

inline constexpr float brickHitFlashSeconds = 0.15f;
inline constexpr float brickBreakBurstSeconds = 0.3f;

struct BrickCell {
    int column = 0;
    int row = 0;
};

class BrickCells {
public:
    BrickCells() = default;
    BrickCells(const BrickCells& other) { append(other); }
    BrickCells(BrickCells&& other) noexcept { take(other); }
    BrickCells& operator=(const BrickCells& other) {
        if (this != &other) {
            cellCount = 0;
            append(other);
        }
        return *this;
    }
    BrickCells& operator=(BrickCells&& other) noexcept {
        if (this != &other) {
            delete[] spilled;
            spilled = nullptr;
            spilledCapacity = 0;
            take(other);
        }
        return *this;
    }
    ~BrickCells() { delete[] spilled; }

    void push_back(BrickCell cell) {
        if (!spilled && cellCount == 0) {
            single = cell;
            cellCount = 1;
            return;
        }
        if (!spilled || cellCount == spilledCapacity) {
            grow(cellCount * 2 + firstSpill);
        }
        spilled[cellCount++] = cell;
    }
    size_t size() const { return cellCount; }
    bool empty() const { return cellCount == 0; }
    BrickCell* begin() { return spilled ? spilled : &single; }
    BrickCell* end() { return begin() + cellCount; }
    const BrickCell* begin() const { return spilled ? spilled : &single; }
    const BrickCell* end() const { return begin() + cellCount; }
    BrickCell& front() { return *begin(); }
    const BrickCell& front() const { return *begin(); }
    BrickCell& operator[](size_t index) { return begin()[index]; }
    const BrickCell& operator[](size_t index) const { return begin()[index]; }

private:
    static constexpr uint32_t firstSpill = 4;

    void grow(uint32_t capacity) {
        BrickCell* larger = new BrickCell[capacity];
        std::copy(begin(), end(), larger);
        delete[] spilled;
        spilled = larger;
        spilledCapacity = capacity;
    }
    void append(const BrickCells& other) {
        for (const BrickCell& cell : other) {
            push_back(cell);
        }
    }
    void take(BrickCells& other) {
        single = other.single;
        spilled = std::exchange(other.spilled, nullptr);
        cellCount = std::exchange(other.cellCount, 0);
        spilledCapacity = std::exchange(other.spilledCapacity, 0);
    }

    BrickCell single{};
    BrickCell* spilled = nullptr;
    uint32_t cellCount = 0;
    uint32_t spilledCapacity = 0;
};

struct Brick {
    int sourceId = emptyBrickId;
    BallColor color = BallColor::Red;
    int maxHits = 1;
    int hitsLeft = 1;
    BrickCells cells;
    float riseOffset = 0.0f;
    float hitFlash = 0.0f;

    float breakBurst = 0.0f;

    bool isAlive() const { return hitsLeft > 0; }
};

class BrickBoard {
public:
    void load(const BrickLayout& layout);

    bool applyHit(int brickIndex);
    bool applyHitsDownTo(int brickIndex, int hitsLeft);
    std::vector<int32_t> hitsLeftPerBrick() const;
    const std::vector<int32_t>& nearBrickFlags() const { return nearFlags; }
    const std::vector<int32_t>& changedCells() const { return dirtyCells; }
    void clearChangedCells();
    bool settleColumns();
    void animate(float deltaTime);

    int columns() const { return columnCount; }
    int rows() const { return rowCount; }
    int paddedWidth() const { return columnCount + 2; }
    int paddedCellCount() const { return (columnCount + 2) * (rowCount + 2); }
    int paddedIndex(int column, int row) const { return (row + 1) * paddedWidth() + column + 1; }

    const std::vector<int32_t>& paddedCells() const { return cells; }
    const std::vector<int32_t>& brickColors() const { return colors; }
    const std::vector<Brick>& bricks() const { return brickList; }
    int brickAt(int column, int row) const { return cells[paddedIndex(column, row)]; }
    int aliveBrickCount() const { return aliveCount; }
    const std::vector<int>& aliveBrickIndices() const { return aliveBricks; }
    const ColorTally& remainingHitsByColor() const { return remainingHits; }
    uint64_t revision() const { return changeRevision; }

    bool isAnimating() const { return !animatingBricks.empty(); }

    const std::vector<int>& animatingBrickIndices() const { return animatingBricks; }
    const BrickCellImage& cellImage() const { return image; }

    void catchUpWith(const BrickBoard& live);
    void trimChangeLog(uint64_t keepFrom) const;
    uint64_t changeLogEnd() const { return changeLogBase + changeLog.size(); }
    uint64_t followedPosition() const { return followed; }
    uint64_t layoutSerial() const { return image.layoutSerial(); }

private:
    int freeRowsAbove(int brickIndex) const;
    bool raiseFully(int brickIndex);
    void startAnimating(int brickIndex);
    void markDead(int brickIndex);
    void placeBrick(int brickIndex, int32_t value);
    void setCell(int index, int32_t value);
    uint8_t shadeOf(int brickIndex) const;
    void reshadeBrick(int brickIndex);
    void markCellChanged(int index);
    enum class LoggedChange : uint32_t { Brick = 0, Cell = 1, Death = 2 };
    void logChange(LoggedChange kind, int index);
    void copyForView(const BrickBoard& live);
    void rebuildNearCounts();
    int lowestRowInColumn(int brickIndex, int column) const;
    void queueFirstBrickBelow(int column, int row);
    void queueBricksBelow(int brickIndex);
    bool settleWholeBoard();

    int columnCount = 0;
    int rowCount = 0;
    std::vector<int32_t> cells;
    std::vector<int32_t> colors;
    std::vector<Brick> brickList;
    std::priority_queue<std::pair<int, int>, std::vector<std::pair<int, int>>, std::greater<>> bricksToSettle;
    bool wholeBoardSettlePending = false;
    std::vector<int32_t> nearCounts;
    std::vector<int32_t> nearFlags;
    std::vector<uint8_t> cellChanged;
    std::vector<int32_t> dirtyCells;
    ColorTally remainingHits{};
    int aliveCount = 0;
    std::vector<int> aliveBricks;
    std::vector<int> alivePosition;
    std::vector<int> animatingBricks;
    std::vector<uint8_t> brickAnimating;
    uint64_t changeRevision = 0;
    BrickCellImage image;

    mutable std::vector<uint32_t> changeLog;
    mutable uint64_t changeLogBase = 0;
    mutable bool logging = false;
    uint64_t followed = 0;
    bool following = false;
};
