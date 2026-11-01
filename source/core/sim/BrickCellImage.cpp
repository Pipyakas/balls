#include "sim/BrickCellImage.h"

#include <algorithm>
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

void BrickCellImage::catchUpWith(const BrickCellImage& source) {
    if (serial != source.serial || revisions.size() != source.revisions.size()) {
        *this = source;
        return;
    }
    for (size_t tile = 0; tile < revisions.size(); ++tile) {
        if (revisions[tile] != source.revisions[tile]) {
            revisions[tile] = source.revisions[tile];
            const auto first = source.shades.begin() + static_cast<std::ptrdiff_t>(tile * cellsPerTile);
            std::copy(first, first + static_cast<std::ptrdiff_t>(cellsPerTile),
                      shades.begin() + static_cast<std::ptrdiff_t>(tile * cellsPerTile));
        }
    }
    nextRevision = source.nextRevision;
}

void BrickCellImage::set(int column, int row, uint8_t shade) {
    uint8_t& cell = shades[indexOf(column, row)];
    if (cell == shade) {
        return;
    }
    cell = shade;
    revisions[static_cast<size_t>(row / tileSize) * tilesX + column / tileSize] = ++nextRevision;
}
