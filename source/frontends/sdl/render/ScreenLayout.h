#pragma once

#include "app/Input.h"
#include "ui/GameplayLayout.h"
#include "ui/Painter.h"

struct BoardTransform {
    Vec2 origin{};
    float cellPixels = 1.0f;

    Vec2 toScreen(float column, float row) const {
        return {origin.x + column * cellPixels, origin.y + row * cellPixels};
    }
    Vec2 toBoard(Vec2 screen) const {
        return {(screen.x - origin.x) / cellPixels, (screen.y - origin.y) / cellPixels};
    }
};

struct GameplayLayout {
    Rect header{};
    Rect mainSection{};
    Rect interactionSection{};
    Rect debugSection{};
    Rect leftGadget{};
    Rect rightGadget{};
    Rect topGadget{};
    Rect bottomGadget{};
    BoardTransform board{};
    Rect boardRect{};
    bool isPortrait = false;
};

GameplayLayout computeGameplayLayout(Vec2 screenSize, int boardColumns, int boardRows);
GameplayLayout computeGameplayLayout(Vec2 screenSize, int boardColumns, int boardRows,
                                     LayoutOrientation orientation, bool compactControls);
