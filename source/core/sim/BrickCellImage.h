#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

class BrickCellImage {
public:
    static constexpr int tileSize = 64;
    static constexpr size_t cellsPerTile = static_cast<size_t>(tileSize) * tileSize;
    static constexpr uint8_t emptyShade = 0;

    static constexpr uint8_t flashBit = 0x80;

    void reset(int columnCount, int rowCount);
    void set(int column, int row, uint8_t shade);

    void catchUpWith(const BrickCellImage& source);
    uint8_t shadeAt(int column, int row) const { return shades[indexOf(column, row)]; }

    static uint8_t brickShade(int colorIndex, bool flashing) {
        return static_cast<uint8_t>((colorIndex + 1) | (flashing ? flashBit : 0));
    }

    int columns() const { return columnCount; }
    int rows() const { return rowCount; }
    int tilesAcross() const { return tilesX; }
    int tilesDown() const { return tilesY; }

    const std::vector<uint8_t>& tileBytes() const { return shades; }
    const std::vector<uint64_t>& tileRevisions() const { return revisions; }

    uint64_t layoutSerial() const { return serial; }

    size_t indexOf(int column, int row) const {
        const size_t tile = static_cast<size_t>(row / tileSize) * tilesX + column / tileSize;
        return tile * cellsPerTile + static_cast<size_t>(row % tileSize) * tileSize + column % tileSize;
    }

private:
    int columnCount = 0;
    int rowCount = 0;
    int tilesX = 0;
    int tilesY = 0;
    std::vector<uint8_t> shades;
    std::vector<uint64_t> revisions;
    uint64_t nextRevision = 0;
    uint64_t serial = 0;
};
