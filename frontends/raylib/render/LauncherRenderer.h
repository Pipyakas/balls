#pragma once

#include "game/LauncherQueue.h"

#include <raylib.h>

struct LauncherSlotHit {
    int column = -1;
    int row = -1;
};

struct LauncherGridLayout {
    Rectangle area{};
    float slotSize = 0.0f;
    float gap = 0.0f;
    Vector2 origin{};

    Rectangle slotRect(int column, float row) const;
};

LauncherGridLayout computeLauncherGrid(Rectangle area, int columns, int visibleRows);
void drawLaunchers(const LauncherQueue& queue, const LauncherGridLayout& grid, bool interactive);
LauncherSlotHit findLauncherAt(const LauncherQueue& queue, const LauncherGridLayout& grid, Vector2 point);
