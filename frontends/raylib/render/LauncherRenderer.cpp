#include "render/LauncherRenderer.h"

#include "render/Palette.h"
#include "ui/Widgets.h"

#include <algorithm>
#include <string>

namespace {

constexpr float gapShare = 0.18f;
constexpr float maximumSlotSize = 110.0f;
constexpr float backRowFade = 0.55f;

std::string compactCount(int count) {
    if (count >= 1000000) {
        return std::to_string(count / 1000000) + "M";
    }
    if (count >= 10000) {
        return std::to_string(count / 1000) + "k";
    }
    return std::to_string(count);
}

}

Rectangle LauncherGridLayout::slotRect(int column, float row) const {
    return {origin.x + static_cast<float>(column) * (slotSize + gap), origin.y + row * (slotSize + gap), slotSize,
            slotSize};
}

LauncherGridLayout computeLauncherGrid(Rectangle area, int columns, int visibleRows) {
    LauncherGridLayout grid;
    grid.area = area;
    const float safeColumns = static_cast<float>(std::max(columns, 1));
    const float safeRows = static_cast<float>(std::max(visibleRows, 1));
    const float byWidth = area.width / (safeColumns + (safeColumns + 1.0f) * gapShare);
    const float byHeight = area.height / (safeRows + (safeRows + 1.0f) * gapShare);
    grid.slotSize = std::min({byWidth, byHeight, maximumSlotSize});
    grid.gap = grid.slotSize * gapShare;
    const float gridWidth = safeColumns * grid.slotSize + (safeColumns - 1.0f) * grid.gap;
    const float gridHeight = safeRows * grid.slotSize + (safeRows - 1.0f) * grid.gap;
    grid.origin = {area.x + (area.width - gridWidth) * 0.5f, area.y + (area.height - gridHeight) * 0.5f};
    return grid;
}

void drawLaunchers(const LauncherQueue& queue, const LauncherGridLayout& grid, bool interactive) {
    const Vector2 mouse = GetMousePosition();
    for (int column = 0; column < queue.columnCount(); ++column) {
        for (int row = 0; row < queue.visibleRows(); ++row) {
            const QueuedLauncher* queued = queue.visible(column, row);
            if (!queued) {
                continue;
            }
            const Rectangle rect = grid.slotRect(column, static_cast<float>(row) + queued->riseOffset);
            Color fill = palette::ballColor(queued->launcher.color);
            if (row > 0) {
                fill = palette::shade(fill, backRowFade + (1.0f - backRowFade) / static_cast<float>(row + 1));
            }
            const bool hovered = interactive && CheckCollisionPointRec(mouse, rect);
            if (hovered) {
                fill = palette::shade(fill, 1.2f);
            }
            const float depth = grid.slotSize * 0.1f;
            DrawRectangleRounded({rect.x, rect.y + depth, rect.width, rect.height - depth}, 0.22f, 8,
                                 palette::shade(fill, 0.6f));
            DrawRectangleRounded({rect.x, rect.y, rect.width, rect.height - depth}, 0.22f, 8, fill);
            const Rectangle labelRect{rect.x, rect.y, rect.width, rect.height - depth};
            const float labelSize = grid.slotSize * 0.36f;
            const std::string label = compactCount(queued->launcher.ballCount);
            ui::drawTextInRect(label, {labelRect.x + 2.0f, labelRect.y + 2.0f, labelRect.width, labelRect.height},
                               labelSize, palette::shade(fill, 0.45f));
            ui::drawTextInRect(label, labelRect, labelSize, palette::text);
        }
    }
}

LauncherSlotHit findLauncherAt(const LauncherQueue& queue, const LauncherGridLayout& grid, Vector2 point) {
    for (int column = 0; column < queue.columnCount(); ++column) {
        for (int row = 0; row < queue.visibleRows(); ++row) {
            const QueuedLauncher* queued = queue.visible(column, row);
            if (queued && CheckCollisionPointRec(point, grid.slotRect(column, static_cast<float>(row) + queued->riseOffset))) {
                return {column, row};
            }
        }
    }
    return {};
}
