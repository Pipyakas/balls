#pragma once

#include "game/SessionView.h"
#include "render/IRenderer.h"
#include "render/ScreenLayout.h"

class BallView {
public:
    explicit BallView(IRenderer& renderer) : renderer(renderer) {}

    // How large one visibility slot is in board cells at this zoom: what the physics thread builds the grid with.
    static float slotCells(const BoardTransform& transform, float radiusCells);
    void draw(const BallFrame& balls, const BoardTransform& transform, float radiusCells);

private:
    IRenderer& renderer;
};
