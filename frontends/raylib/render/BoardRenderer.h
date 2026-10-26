#pragma once

#include "render/ScreenLayout.h"
#include "sim/BrickBoard.h"

void drawBoardBackground(const BrickBoard& board, const BoardTransform& transform);
void drawBricks(const BrickBoard& board, const BoardTransform& transform);
void drawLaunchPoint(Vector2 launchCell, const BoardTransform& transform);
