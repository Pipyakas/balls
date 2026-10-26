#pragma once

#include "render/ScreenLayout.h"
#include "sim/PhysicsBackend.h"

#include <raylib.h>

#include <chrono>

class BallRenderer {
public:
    void load();
    void unload();
    void draw(PhysicsBackend& backend, const BoardTransform& transform, float radiusCells);
    double takeSynchronizationMs();

private:
    bool ensureCapacity(size_t ballCount);
    void shareWith(PhysicsBackend& backend);

    Shader shader{};
    int mvpLocation = -1;
    int originLocation = -1;
    int cellPixelsLocation = -1;
    int radiusLocation = -1;
    int unpackScaleLocation = -1;
    int paletteLocation = -1;
    unsigned int vertexArray = 0;
    unsigned int packedBuffer = 0;
    size_t capacity = 0;
    const PhysicsBackend* sharedBackend = nullptr;
    bool sharedBufferAccepted = false;
    double synchronizationMs = 0.0;
};
