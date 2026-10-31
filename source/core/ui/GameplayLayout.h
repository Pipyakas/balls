#pragma once

#include "game/Settings.h"

struct LayoutPoint {
    float x = 0.0f;
    float y = 0.0f;
};

struct LayoutRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct CoreGameplayLayout {
    LayoutRect header{};
    LayoutRect main{};
    LayoutRect interaction{};
    LayoutRect debug{};
    LayoutRect leftGadget{};
    LayoutRect rightGadget{};
    LayoutRect topGadget{};
    LayoutRect bottomGadget{};
    LayoutPoint boardOrigin{};
    float cellSize = 1.0f;
    LayoutRect boardRect{};
    bool isPortrait = false;
};

LayoutOrientation resolveOrientation(LayoutOrientation setting, float width, float height);
CoreGameplayLayout computeCoreGameplayLayout(float frameWidth, float frameHeight, int boardColumns,
                                             int boardRows, LayoutOrientation orientation,
                                             int launcherRows,
                                             WideScreenAnchor wideScreen = WideScreenAnchor::ScreenEdges);
