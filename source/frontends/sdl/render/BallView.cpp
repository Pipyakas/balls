#include "render/BallView.h"

#include "render/Palette.h"
#include "ui/Painter.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace {

constexpr float channelScale = 1.0f / 255.0f;

}

double BallView::takeSynchronizationMs() {
    const double taken = synchronizationMs;
    synchronizationMs = 0.0;
    return taken;
}

void BallView::draw(PhysicsBackend& backend, const BoardTransform& transform, float radiusCells) {
    if (backend.ballCount() == 0) {
        return;
    }
    const float slotSize = visibilitySlotSize(radiusCells, transform.cellPixels);
    backend.setVisibilityGrid(visibilitySlots(lastFieldWidth, slotSize), visibilitySlots(lastFieldHeight, slotSize));
    const auto snapshotStart = std::chrono::steady_clock::now();
    const BallSnapshot balls = backend.snapshot();
    synchronizationMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - snapshotStart).count();
    lastFieldWidth = balls.fieldWidth;
    lastFieldHeight = balls.fieldHeight;
    if (balls.count == 0 || !balls.packed) {
        return;
    }
    BallDrawRequest request;
    request.packed = balls.packed;
    request.count = balls.count;
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
