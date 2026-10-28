#include "render/BallView.h"

#include "render/Palette.h"
#include "ui/Painter.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace {

constexpr float channelScale = 1.0f / 255.0f;

}

float BallView::slotCells(const BoardTransform& transform, float radiusCells) {
    return visibilitySlotSize(radiusCells, transform.cellPixels);
}

void BallView::draw(const BallFrame& balls, const BoardTransform& transform, float radiusCells) {
    if (balls.packed.empty()) {
        return;
    }
    BallDrawRequest request;
    request.packed = balls.packed.data();
    request.count = balls.packed.size();
    request.isVisibilityGrid = balls.visibilityGrid;
    request.fieldWidth = balls.fieldWidth;
    request.fieldHeight = balls.fieldHeight;
    request.originX = transform.origin.x;
    request.originY = transform.origin.y;
    request.cellPixels = transform.cellPixels;
    request.radiusCells = radiusCells;
    request.slotsPerRadius = visibilitySlotsPerRadius;
    const Rect clip = gfx::currentClip();
    request.clipX = clip.x;
    request.clipY = clip.y;
    request.clipWidth = clip.width;
    request.clipHeight = clip.height;
    for (int color = 0; color < rendererPaletteSize; ++color) {
        const Color tint = palette::ballColor(color);
        request.palette[color] = {tint.r * channelScale, tint.g * channelScale, tint.b * channelScale, 1.0f};
    }
    renderer.drawBalls(request);
}
