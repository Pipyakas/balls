#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// What every board cell shows, one byte per cell, for front ends that draw a board too large for one shape per
// brick. Cells are stored in square tiles, each tile contiguous, so a renderer can re-upload only the tiles whose
// revision moved since it last looked: a hit or a fall touches a few tiles, not the whole board.
class BrickCellImage {
public:
    static constexpr int tileSize = 64;
    static constexpr size_t cellsPerTile = static_cast<size_t>(tileSize) * tileSize;
    static constexpr uint8_t emptyShade = 0;
    // A brick cell is its color index + 1, with this bit set while the brick shows its hit flash.
    static constexpr uint8_t flashBit = 0x80;

    void reset(int columnCount, int rowCount);
    void set(int column, int row, uint8_t shade);
    uint8_t shadeAt(int column, int row) const { return shades[indexOf(column, row)]; }

    static uint8_t brickShade(int colorIndex, bool flashing) {
        return static_cast<uint8_t>((colorIndex + 1) | (flashing ? flashBit : 0));
    }

    int columns() const { return columnCount; }
    int rows() const { return rowCount; }
    int tilesAcross() const { return tilesX; }
    int tilesDown() const { return tilesY; }
    // Tile-major: tile (tx, ty) holds cellsPerTile bytes starting at (ty * tilesAcross() + tx) * cellsPerTile.
    const std::vector<uint8_t>& tileBytes() const { return shades; }
    const std::vector<uint64_t>& tileRevisions() const { return revisions; }
    // Changes whenever the board is loaded again, so a renderer knows its copy belongs to another board.
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
