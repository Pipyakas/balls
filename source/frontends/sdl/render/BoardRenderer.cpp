#include "render/BoardRenderer.h"

#include "render/Palette.h"
#include "ui/Widgets.h"

#include <algorithm>
#include <string>

namespace {

constexpr float cellInset = 0.06f;
constexpr float bevelShare = 0.16f;
constexpr float minimumLabelPixels = 9.0f;
constexpr float minimumBevelPixels = 4.0f;
constexpr float boardFrameWidth = 8.0f;
constexpr float boardFrameRadius = 10.0f;

struct CellEdges {
    bool left;
    bool right;
    bool top;
    bool bottom;
};

CellEdges openEdges(const BrickBoard& board, int brickIndex, const BrickCell& cell) {
    auto differs = [&](int column, int row) {
        const bool inside = column >= 0 && column < board.columns() && row >= 0 && row < board.rows();
        return !inside || board.brickAt(column, row) != brickIndex;
    };
    return {differs(cell.column - 1, cell.row), differs(cell.column + 1, cell.row), differs(cell.column, cell.row - 1),
            differs(cell.column, cell.row + 1)};
}

void drawBrickCell(const BoardTransform& transform, float column, float row, CellEdges edges, Color color) {
    const float size = transform.cellPixels;
    const float inset = size * cellInset;
    const float left = column * size + (edges.left ? inset : 0.0f);
    const float right = (column + 1.0f) * size - (edges.right ? inset : 0.0f);
    const float top = row * size + (edges.top ? inset : 0.0f);
    const float bottom = (row + 1.0f) * size - (edges.bottom ? inset : 0.0f);
    const Vec2 origin = transform.origin;
    const float bevel = std::max(1.0f, size * bevelShare);
    gfx::sceneRect({origin.x + left, origin.y + top, right - left, bottom - top}, color);
    if (edges.bottom) {
        gfx::sceneRect({origin.x + left, origin.y + bottom - bevel, right - left, bevel}, palette::shade(color, 0.7f));
    }
    if (edges.top) {
        const float highlightLeft = left + (edges.left ? bevel * 0.6f : 0.0f);
        const float highlightRight = right - (edges.right ? bevel * 0.6f : 0.0f);
        gfx::sceneRect({origin.x + highlightLeft, origin.y + top + bevel * 0.5f, highlightRight - highlightLeft, bevel * 0.6f},
                       palette::shade(color, 1.25f));
    }
}

Rect labelArea(const Brick& brick, const BoardTransform& transform) {
    int minColumn = brick.cells.front().column;
    int maxColumn = minColumn;
    int minRow = brick.cells.front().row;
    int maxRow = minRow;
    float sumColumn = 0.0f;
    float sumRow = 0.0f;
    for (const BrickCell& cell : brick.cells) {
        minColumn = std::min(minColumn, cell.column);
        maxColumn = std::max(maxColumn, cell.column);
        minRow = std::min(minRow, cell.row);
        maxRow = std::max(maxRow, cell.row);
        sumColumn += static_cast<float>(cell.column) + 0.5f;
        sumRow += static_cast<float>(cell.row) + 0.5f;
    }
    const int boxArea = (maxColumn - minColumn + 1) * (maxRow - minRow + 1);
    const float rise = brick.riseOffset;
    if (boxArea == static_cast<int>(brick.cells.size())) {
        const Vec2 topLeft = transform.toScreen(static_cast<float>(minColumn), static_cast<float>(minRow) + rise);
        const Vec2 bottomRight = transform.toScreen(static_cast<float>(maxColumn + 1), static_cast<float>(maxRow + 1) + rise);
        return {topLeft.x, topLeft.y, bottomRight.x - topLeft.x, bottomRight.y - topLeft.y};
    }
    const float count = static_cast<float>(brick.cells.size());
    const Vec2 centroid{sumColumn / count, sumRow / count};
    const BrickCell* nearest = &brick.cells.front();
    float nearestDistance = 1e9f;
    for (const BrickCell& cell : brick.cells) {
        const float dx = static_cast<float>(cell.column) + 0.5f - centroid.x;
        const float dy = static_cast<float>(cell.row) + 0.5f - centroid.y;
        if (dx * dx + dy * dy < nearestDistance) {
            nearestDistance = dx * dx + dy * dy;
            nearest = &cell;
        }
    }
    const Vec2 topLeft = transform.toScreen(static_cast<float>(nearest->column), static_cast<float>(nearest->row) + rise);
    return {topLeft.x, topLeft.y, transform.cellPixels, transform.cellPixels};
}

}

void drawBoardBackground(const BrickBoard& board, const BoardTransform& transform) {
    const float width = static_cast<float>(board.columns()) * transform.cellPixels;
    const float height = static_cast<float>(board.rows()) * transform.cellPixels;
    const Rect area{transform.origin.x, transform.origin.y, width, height};
    gfx::sceneRect(ui::shrink(area, -boardFrameWidth), palette::frame, boardFrameRadius);
    gfx::sceneRect(area, palette::backgroundDeep);
}

void drawBricks(const BrickBoard& board, const BoardTransform& transform) {
    const auto& bricks = board.bricks();
    for (const int index : board.aliveBrickIndices()) {
        const Brick& brick = bricks[static_cast<size_t>(index)];
        Color color = palette::brickColor(brick.color);
        if (brick.hitFlash > 0.0f) {
            color = palette::shade(color, 1.4f);
        }
        if (transform.cellPixels < minimumBevelPixels) {
            for (const BrickCell& cell : brick.cells) {
                const Vec2 topLeft = transform.toScreen(static_cast<float>(cell.column),
                                                        static_cast<float>(cell.row) + brick.riseOffset);
                gfx::sceneRect({topLeft.x, topLeft.y, transform.cellPixels, transform.cellPixels}, color);
            }
            continue;
        }
        for (const BrickCell& cell : brick.cells) {
            drawBrickCell(transform, static_cast<float>(cell.column), static_cast<float>(cell.row) + brick.riseOffset,
                          openEdges(board, index, cell), color);
        }
        if (brick.maxHits <= 1) {
            continue;
        }
        const Rect bounds = labelArea(brick, transform);
        const float labelSize = std::min(bounds.width, bounds.height) * 0.55f;
        if (labelSize < minimumLabelPixels) {
            continue;
        }
        const std::string label = std::to_string(brick.hitsLeft);
        ui::drawTextInRect(label, {bounds.x + 1.5f, bounds.y + 1.5f, bounds.width, bounds.height}, labelSize,
                           palette::shade(color, 0.4f));
        ui::drawTextInRect(label, bounds, labelSize, palette::text);
    }
}

void drawLaunchPoint(Vec2 launchCell, const BoardTransform& transform) {
    const Vec2 center = transform.toScreen(launchCell.x, launchCell.y);
    const float size = transform.cellPixels * 0.6f;
    gfx::fillTriangle({center.x, center.y - size * 0.6f}, {center.x - size, center.y + size * 0.4f},
                 {center.x + size, center.y + size * 0.4f}, palette::shade(palette::frame, 1.2f));
}
