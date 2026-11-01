#pragma once

#include "game/LauncherQueue.h"

#include "app/Input.h"
#include "ui/Painter.h"

struct LauncherSlotHit {
    int column = -1;
    int row = -1;
};

struct LauncherGridLayout {
    Rect area{};
    float slotSize = 0.0f;
    float gap = 0.0f;
    Vec2 origin{};

    Rect slotRect(int column, float row) const;
};

LauncherGridLayout computeLauncherGrid(Rect area, int columns, int visibleRows);
LauncherGridLayout computePortraitLauncherGrid(Rect area, int columns, int visibleRows);
float portraitRowWidth(const LauncherQueue& queue, const LauncherGridLayout& grid);
void drawLaunchers(const LauncherQueue& queue, const LauncherGridLayout& grid, bool interactive);
void drawPortraitLaunchers(const LauncherQueue& queue, const LauncherGridLayout& grid, float scroll,
                           bool interactive);
LauncherSlotHit findLauncherAt(const LauncherQueue& queue, const LauncherGridLayout& grid, Vec2 point);
LauncherSlotHit findPortraitLauncherAt(const LauncherQueue& queue, const LauncherGridLayout& grid,
                                       float scroll, Vec2 point);
