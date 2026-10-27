#include "render/LauncherRenderer.h"

#include "render/Palette.h"
#include "ui/Widgets.h"

#include <algorithm>
#include <string>

namespace {

constexpr float gapShare = 0.18f;
constexpr float maximumSlotSize = 110.0f;
constexpr float backRowFade = 0.55f;
constexpr float portraitLauncherFraction = 0.19f;
constexpr float portraitLauncherMinimum = 120.0f;
constexpr float portraitLauncherMaximum = 240.0f;

void drawLauncherSlot(Rect rect, float slotSize, Color fill, const std::string& label, float labelSize) {
    const float depth = slotSize * 0.1f;
    gfx::fillRounded({rect.x, rect.y + depth, rect.width, rect.height - depth}, 0.22f,
                     palette::shade(fill, 0.6f));
    gfx::fillRounded({rect.x, rect.y, rect.width, rect.height - depth}, 0.22f, fill);
    const Rect labelRect{rect.x, rect.y, rect.width, rect.height - depth};
    ui::drawTextInRect(label, {labelRect.x + 2.0f, labelRect.y + 2.0f, labelRect.width, labelRect.height},
                       labelSize, palette::shade(fill, 0.45f));
    ui::drawTextInRect(label, labelRect, labelSize, palette::textStrong);
}

std::string compactCount(int count) {
    if (count >= 1000000) {
        return std::to_string(count / 1000000) + "M";
    }
    if (count >= 10000) {
        return std::to_string(count / 1000) + "k";
    }
    return std::to_string(count);
}

Color launcherFill(const QueuedLauncher& queued, int row, bool hovered) {
    Color fill = palette::ballColor(queued.launcher.color);
    if (row > 0) {
        fill = palette::shade(fill, backRowFade + (1.0f - backRowFade) / static_cast<float>(row + 1));
    }
    return hovered ? palette::shade(fill, 1.2f) : fill;
}

Rect scrolledSlotRect(const LauncherGridLayout& grid, int column, float row, float scroll) {
    Rect rect = grid.slotRect(column, row);
    rect.x -= scroll;
    return rect;
}

} // namespace

Rect LauncherGridLayout::slotRect(int column, float row) const {
    return {origin.x + static_cast<float>(column) * (slotSize + gap), origin.y + row * (slotSize + gap),
            slotSize, slotSize};
}

LauncherGridLayout computeLauncherGrid(Rect area, int columns, int visibleRows) {
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

LauncherGridLayout computePortraitLauncherGrid(Rect area, int columns, int visibleRows) {
    LauncherGridLayout grid;
    grid.area = area;
    const float rows = static_cast<float>(std::max(visibleRows, 1));
    const float byWidth =
        std::clamp(area.width * portraitLauncherFraction, portraitLauncherMinimum, portraitLauncherMaximum);
    const float byHeight = std::max(area.height - 24.0f, 1.0f) / (rows + (rows - 1.0f) * gapShare);
    grid.slotSize = std::max(1.0f, std::min(byWidth, byHeight));
    grid.gap = grid.slotSize * gapShare;
    const float gridHeight = rows * grid.slotSize + (rows - 1.0f) * grid.gap;
    const float safeColumns = static_cast<float>(std::max(columns, 1));
    const float gridWidth = safeColumns * grid.slotSize + (safeColumns - 1.0f) * grid.gap;
    const float centeredX = area.x + (area.width - gridWidth) * 0.5f;
    grid.origin = {std::max(area.x + 6.0f, centeredX), area.y + (area.height - gridHeight) * 0.5f};
    return grid;
}

bool portraitSlotVisible(Rect rect, Rect area) {
    return rect.x + rect.width > area.x && rect.x < area.x + area.width;
}

void drawLaunchers(const LauncherQueue& queue, const LauncherGridLayout& grid, bool interactive) {
    const Vec2 mouse = input::mousePosition();
    for (int column = 0; column < queue.columnCount(); ++column) {
        for (int row = 0; row < queue.visibleRows(); ++row) {
            const QueuedLauncher* queued = queue.visible(column, row);
            if (!queued) {
                continue;
            }
            const Rect rect = grid.slotRect(column, static_cast<float>(row) + queued->riseOffset);
            const Color fill = launcherFill(*queued, row, interactive && pointInRect(mouse, rect));
            drawLauncherSlot(rect, grid.slotSize, fill, compactCount(queued->launcher.ballCount),
                             grid.slotSize * 0.36f);
        }
    }
}

void drawPortraitLaunchers(const LauncherQueue& queue, const LauncherGridLayout& grid, float scroll,
                           bool interactive) {
    const Vec2 mouse = input::mousePosition();
    for (int column = 0; column < queue.columnCount(); ++column) {
        for (int row = 0; row < queue.visibleRows(); ++row) {
            const QueuedLauncher* queued = queue.visible(column, row);
            if (!queued) {
                continue;
            }
            const Rect rect = scrolledSlotRect(grid, column, static_cast<float>(row) + queued->riseOffset, scroll);
            if (!portraitSlotVisible(rect, grid.area)) {
                continue;
            }
            const Color fill = launcherFill(*queued, row, interactive && pointInRect(mouse, rect));
            drawLauncherSlot(rect, grid.slotSize, fill, compactCount(queued->launcher.ballCount),
                             grid.slotSize * 0.36f);
        }
    }
}

float portraitRowWidth(const LauncherQueue& queue, const LauncherGridLayout& grid) {
    if (queue.columnCount() == 0) {
        return 0.0f;
    }
    return static_cast<float>(queue.columnCount()) * (grid.slotSize + grid.gap) - grid.gap;
}

LauncherSlotHit findLauncherAt(const LauncherQueue& queue, const LauncherGridLayout& grid, Vec2 point) {
    for (int column = 0; column < queue.columnCount(); ++column) {
        for (int row = 0; row < queue.visibleRows(); ++row) {
            const QueuedLauncher* queued = queue.visible(column, row);
            if (queued &&
                pointInRect(point, grid.slotRect(column, static_cast<float>(row) + queued->riseOffset))) {
                return {column, row};
            }
        }
    }
    return {};
}

LauncherSlotHit findPortraitLauncherAt(const LauncherQueue& queue, const LauncherGridLayout& grid,
                                       float scroll, Vec2 point) {
    for (int column = 0; column < queue.columnCount(); ++column) {
        for (int row = 0; row < queue.visibleRows(); ++row) {
            const QueuedLauncher* queued = queue.visible(column, row);
            if (queued && pointInRect(point, scrolledSlotRect(grid, column,
                                                              static_cast<float>(row) + queued->riseOffset, scroll))) {
                return {column, row};
            }
        }
    }
    return {};
}
