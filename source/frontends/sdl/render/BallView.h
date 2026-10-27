#pragma once

#include "render/IRenderer.h"
#include "render/ScreenLayout.h"
#include "sim/PhysicsBackend.h"

class BallView {
public:
    explicit BallView(IRenderer& renderer) : renderer(renderer) {}

    void draw(PhysicsBackend& backend, const BoardTransform& transform, float radiusCells);
    double takeSynchronizationMs();

private:
    IRenderer& renderer;
    double synchronizationMs = 0.0;
    float lastFieldWidth = 0.0f;
    float lastFieldHeight = 0.0f;
};
