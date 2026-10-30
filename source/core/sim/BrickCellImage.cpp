#include "sim/BrickCellImage.h"

#include <atomic>

namespace {

std::atomic<uint64_t> lastLayoutSerial{0};

}

void BrickCellImage::reset(int columnCount, int rowCount) {
    this->columnCount = columnCount;
    this->rowCount = rowCount;
    tilesX = (columnCount + tileSize - 1) / tileSize;
    tilesY = (rowCount + tileSize - 1) / tileSize;
    shades.assign(static_cast<size_t>(tilesX) * tilesY * cellsPerTile, emptyShade);
    nextRevision = 1;
    revisions.assign(static_cast<size_t>(tilesX) * tilesY, nextRevision);
    serial = ++lastLayoutSerial;
}

void BrickCellImage::set(int column, int row, uint8_t shade) {
    uint8_t& cell = shades[indexOf(column, row)];
    if (cell == shade) {
        return;
    }
    cell = shade;
    revisions[static_cast<size_t>(row / tileSize) * tilesX + column / tileSize] = ++nextRevision;
}
