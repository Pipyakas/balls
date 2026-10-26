#pragma once

#include "render/ScreenLayout.h"
#include "sim/PhysicsBackend.h"

#include <raylib.h>

class BallRenderer {
public:
    void load();
    void unload();
    void draw(const BallSnapshot& balls, const BoardTransform& transform, float radiusCells);

private:
    void ensureCapacity(size_t ballCount);

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
};
