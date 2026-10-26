#pragma once

#include "level/LevelData.h"

#include <cstdint>
#include <vector>

struct BrickCell {
    int column = 0;
    int row = 0;
};

struct Brick {
    int sourceId = emptyBrickId;
    BallColor color = BallColor::Red;
    int maxHits = 1;
    int hitsLeft = 1;
    std::vector<BrickCell> cells;
    float riseOffset = 0.0f;
    float hitFlash = 0.0f;

    bool isAlive() const { return hitsLeft > 0; }
};

class BrickBoard {
public:
    void load(const BrickLayout& layout);

    bool applyHit(int brickIndex);
    bool applyHitsDownTo(int brickIndex, int hitsLeft);
    std::vector<int32_t> hitsLeftPerBrick() const;
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
    const ColorTally& remainingHitsByColor() const { return remainingHits; }
    uint64_t revision() const { return changeRevision; }

private:
    bool tryRaise(int brickIndex);
    void placeBrick(int brickIndex, int32_t value);

    int columnCount = 0;
    int rowCount = 0;
    std::vector<int32_t> cells;
    std::vector<int32_t> colors;
    std::vector<Brick> brickList;
    ColorTally remainingHits{};
    int aliveCount = 0;
    uint64_t changeRevision = 0;
};
