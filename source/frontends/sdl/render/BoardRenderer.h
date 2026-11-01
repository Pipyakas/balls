#pragma once

#include "render/IRenderer.h"
#include "render/ScreenLayout.h"
#include "sim/BrickBoard.h"

void drawBoardBackground(const BrickBoard& board, const BoardTransform& transform);
void drawBricks(IRenderer& renderer, const BrickBoard& board, const BoardTransform& transform);
void drawLaunchPoint(Vec2 launchCell, const BoardTransform& transform);

void drawBrickEffects(const BrickBoard& board, const BoardTransform& transform, float ballRadiusCells);
