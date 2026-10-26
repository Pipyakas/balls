#pragma once

#include "render/ScreenLayout.h"
#include "sim/BrickBoard.h"

void drawBoardBackground(const BrickBoard& board, const BoardTransform& transform);
void drawBricks(const BrickBoard& board, const BoardTransform& transform);
void drawLaunchPoint(Vec2 launchCell, const BoardTransform& transform);
