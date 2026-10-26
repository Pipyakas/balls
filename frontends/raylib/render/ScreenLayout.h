#pragma once

#include <raylib.h>

struct BoardTransform {
    Vector2 origin{};
    float cellPixels = 1.0f;

    Vector2 toScreen(float column, float row) const {
        return {origin.x + column * cellPixels, origin.y + row * cellPixels};
    }
    Vector2 toBoard(Vector2 screen) const {
        return {(screen.x - origin.x) / cellPixels, (screen.y - origin.y) / cellPixels};
    }
};

struct GameplayLayout {
    Rectangle header{};
    Rectangle mainSection{};
    Rectangle interactionSection{};
    Rectangle debugSection{};
    BoardTransform board{};
    Rectangle boardRect{};
};

GameplayLayout computeGameplayLayout(Vector2 screenSize, int boardColumns, int boardRows);
