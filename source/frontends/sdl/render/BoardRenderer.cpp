#include "render/BoardRenderer.h"

#include "render/Palette.h"
#include "ui/Widgets.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>

namespace {

constexpr float cellInset = 0.06f;
constexpr float bevelShare = 0.16f;
constexpr float minimumLabelPixels = 9.0f;
constexpr float minimumBevelPixels = 4.0f;
constexpr float boardFrameWidth = 1.0f;
// A hit's ring spreads to this many ball radii while the ball shrinks away.
constexpr float hitRingGrowth = 2.5f;
constexpr float hitRingThickness = 2.0f;
// Each cell of a broken brick bursts into this many pieces, which travel this far (in cells) as they shrink and fade.
constexpr int burstPiecesPerCell = 8;
constexpr float burstTravelCells = 0.9f;
constexpr float burstPieceShare = 0.3f;

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

// The ball that hit a brick disappears into it: a ring in its color spreads from the contact point while the ball
// shrinks away. Hits are only known per brick, and balls come up from the launch point, so the contact point drawn is
// the middle of the brick's bottom edge.
void drawHitPulse(const Brick& brick, const BoardTransform& transform, float ballRadiusPixels) {
    int minColumn = brick.cells.front().column;
    int maxColumn = minColumn;
    int maxRow = brick.cells.front().row;
    for (const BrickCell& cell : brick.cells) {
        minColumn = std::min(minColumn, cell.column);
        maxColumn = std::max(maxColumn, cell.column);
        maxRow = std::max(maxRow, cell.row);
    }
    const float progress = 1.0f - brick.hitFlash / brickHitFlashSeconds;
    const Vec2 contact = transform.toScreen(static_cast<float>(minColumn + maxColumn + 1) * 0.5f,
                                            static_cast<float>(maxRow + 1) + brick.riseOffset);
    const Color color = palette::ballColor(brick.color);
    const float ringRadius = ballRadiusPixels * (1.0f + (hitRingGrowth - 1.0f) * progress);
    gfx::strokeRounded({contact.x - ringRadius, contact.y - ringRadius, ringRadius * 2.0f, ringRadius * 2.0f}, 1.0f,
                       hitRingThickness, gfx::fade(color, 1.0f - progress));
    gfx::fillCircle(contact, ballRadiusPixels * (1.0f - progress), color);
}

// A broken brick bursts into pieces of its color that fly out of each of its cells, shrinking and fading.
void drawBreakBurst(const Brick& brick, const BoardTransform& transform) {
    const float progress = 1.0f - brick.breakBurst / brickBreakBurstSeconds;
    const Color color = gfx::fade(palette::brickColor(brick.color), 1.0f - progress);
    const float pieceSize = transform.cellPixels * burstPieceShare * (1.0f - progress);
    const float travel = burstTravelCells * progress;
    for (const BrickCell& cell : brick.cells) {
        // Each cell turns its pieces by a different angle, so neighbouring cells do not burst in step.
        const float turn = static_cast<float>(cell.column * 7 + cell.row * 13);
        for (int piece = 0; piece < burstPiecesPerCell; ++piece) {
            const float angle = (static_cast<float>(piece) + 0.5f) * 2.0f * std::numbers::pi_v<float> /
                                    static_cast<float>(burstPiecesPerCell) + turn;
            const Vec2 center = transform.toScreen(static_cast<float>(cell.column) + 0.5f + std::cos(angle) * travel,
                                                   static_cast<float>(cell.row) + 0.5f + brick.riseOffset +
                                                       std::sin(angle) * travel);
            gfx::fillRect({center.x - pieceSize * 0.5f, center.y - pieceSize * 0.5f, pieceSize, pieceSize}, color);
        }
    }
}

}

void drawBoardBackground(const BrickBoard& board, const BoardTransform& transform) {
    const float width = static_cast<float>(board.columns()) * transform.cellPixels;
    const float height = static_cast<float>(board.rows()) * transform.cellPixels;
    const Rect area{transform.origin.x, transform.origin.y, width, height};
    gfx::sceneRect(ui::shrink(area, -boardFrameWidth), palette::border);
    gfx::sceneRect(area, palette::boardBackdrop);
}

void drawBricks(const BrickBoard& board, const BoardTransform& transform) {
    const auto& bricks = board.bricks();
    for (const int index : board.aliveBrickIndices()) {
        const Brick& brick = bricks[static_cast<size_t>(index)];
        Color color = palette::brickColor(brick.color);
        if (brick.hitFlash > 0.0f) {
            color = palette::shade(color, theme::brickHitFlash);
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
        ui::drawTextInRect(label, bounds, labelSize, palette::textStrong);
    }
}

void drawLaunchPoint(Vec2 launchCell, const BoardTransform& transform) {
    const Vec2 center = transform.toScreen(launchCell.x, launchCell.y);
    const float size = transform.cellPixels * 0.6f;
    gfx::fillTriangle({center.x, center.y - size * 0.6f}, {center.x - size, center.y + size * 0.4f},
                 {center.x + size, center.y + size * 0.4f}, palette::textDim);
}

void drawBrickEffects(const BrickBoard& board, const BoardTransform& transform, float ballRadiusCells) {
    const auto& bricks = board.bricks();
    const float ballRadiusPixels = ballRadiusCells * transform.cellPixels;
    for (const int index : board.animatingBrickIndices()) {
        const Brick& brick = bricks[static_cast<size_t>(index)];
        if (brick.hitFlash > 0.0f) {
            drawHitPulse(brick, transform, ballRadiusPixels);
        }
        if (brick.breakBurst > 0.0f) {
            drawBreakBurst(brick, transform);
        }
    }
}
